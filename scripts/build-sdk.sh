#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
# NIL_* is canonical; NY_* environment aliases remain accepted during migration.
build_root="${NIL_BUILD_DIR:-${NY_BUILD_DIR:-${repo_root}/build}}"
build_type="${NIL_BUILD_TYPE:-${NY_BUILD_TYPE:-Release}}"
enable_llvm="${NIL_ENABLE_LLVM:-${NY_ENABLE_LLVM:-ON}}"
build_experimental="${NIL_BUILD_EXPERIMENTAL_TOOLS:-${NY_BUILD_EXPERIMENTAL_TOOLS:-ON}}"
run_tests="${NIL_RUN_TESTS:-${NY_RUN_TESTS:-ON}}"
allow_dirty_source="${NIL_ALLOW_DIRTY_SOURCE:-${NY_ALLOW_DIRTY_SOURCE:-OFF}}"
qualification="${NIL_SDK_QUALIFICATION:-${NY_SDK_QUALIFICATION:-development}}"

if [[ "${build_root}" == "/" ]]; then
    printf 'Refusing unsafe build root: %s\n' "${build_root}" >&2
    exit 2
fi

for boolean_name in enable_llvm build_experimental run_tests allow_dirty_source; do
    boolean_value="${!boolean_name}"
    if [[ "${boolean_value}" != "ON" && "${boolean_value}" != "OFF" ]]; then
        printf '%s must be ON or OFF (received %s)\n' \
            "${boolean_name}" "${boolean_value}" >&2
        exit 2
    fi
done

if [[ "${qualification}" != "development" && "${qualification}" != "rc-checks" ]]; then
    printf 'NIL_SDK_QUALIFICATION must be development or rc-checks\n' >&2
    exit 2
fi
if [[ "${qualification}" == "rc-checks" ]]; then
    if [[ "${run_tests}" != "ON" || "${enable_llvm}" != "ON" || \
          "${allow_dirty_source}" != "OFF" || "${build_type}" != "Release" ]]; then
        printf '%s\n' 'RC SDK checks require tests ON, LLVM ON, dirty-source allowance OFF, and Release.' >&2
        exit 2
    fi
    if [[ "$(python3 -c 'import platform; print(platform.system() + ":" + platform.machine())')" != "Linux:x86_64" ]]; then
        printf 'RC SDK checks currently support only Linux x86_64\n' >&2
        exit 2
    fi
fi

if [[ "${build_experimental}" != "ON" ]]; then
    printf '%s\n' \
        'NIL_BUILD_EXPERIMENTAL_TOOLS must be ON to package the SDK schema and agent tools' >&2
    exit 2
fi

if [[ -L "${build_root}" ]]; then
    printf 'Refusing a symlinked build root: %s\n' "${build_root}" >&2
    exit 2
fi
build_root="$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "${build_root}")"
if [[ "${build_root}" == "/" || "${build_root}" == "${repo_root}" ]]; then
    printf 'Refusing unsafe build root: %s\n' "${build_root}" >&2
    exit 2
fi
cmake_build_dir="${build_root}/cmake"
sdk_dir="${NIL_SDK_DIR:-${NY_SDK_DIR:-${repo_root}/sdk}}"

if [[ "${sdk_dir}" == "/" ]]; then
    printf 'Refusing unsafe SDK destination: %s\n' "${sdk_dir}" >&2
    exit 2
fi
sdk_dir="$(python3 -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "${sdk_dir}")"
sdk_parent="$(dirname "${sdk_dir}")"
sdk_name="$(basename "${sdk_dir}")"
mkdir -p "${sdk_parent}"
sdk_parent="$(cd "${sdk_parent}" && pwd -P)"
sdk_dir="${sdk_parent}/${sdk_name}"
sdk_store_dir="${NIL_SDK_STORE_DIR:-${NY_SDK_STORE_DIR:-${sdk_parent}/.${sdk_name}.store}}"
mkdir -p "${sdk_store_dir}"
sdk_store_dir="$(cd "${sdk_store_dir}" && pwd -P)"

if [[ "${sdk_dir}" == "/" || "${sdk_dir}" == "${repo_root}" || \
      "${sdk_store_dir}" == "/" || "${sdk_store_dir}" == "${repo_root}" ]]; then
    printf 'Refusing unsafe SDK destination: %s\n' "${sdk_dir}" >&2
    exit 2
fi
if [[ "${sdk_dir}" == "${sdk_store_dir}" || \
      "${sdk_dir}" == "${sdk_store_dir}/"* || \
      "${sdk_store_dir}" == "${sdk_dir}/"* ]]; then
    printf 'SDK destination and generation store must not contain one another\n' >&2
    exit 2
fi

revision="$(git -C "${repo_root}" rev-parse --verify HEAD)"
source_status="$(git -C "${repo_root}" status --porcelain=v1 --untracked-files=normal)"
if [[ -n "${source_status}" && "${allow_dirty_source}" != "ON" ]]; then
    printf '%s\n' \
        'Refusing to build an SDK from a dirty NIL Framework worktree.' \
        'Commit/stash the changes, or set NIL_ALLOW_DIRTY_SOURCE=ON for an explicitly marked development SDK.' >&2
    exit 2
fi

cmake \
    -S "${repo_root}" \
    -B "${cmake_build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DNIL_ENABLE_LLVM="${enable_llvm}" \
    -DNIL_BUILD_EXPERIMENTAL_TOOLS="${build_experimental}" \
    -DBUILD_TESTING="${run_tests}" \
    -DCMAKE_INSTALL_BINDIR=.

cmake --build "${cmake_build_dir}" --parallel

evidence_path="${cmake_build_dir}/sdk-build-evidence.json"
ctest_report="${cmake_build_dir}/sdk-ctest.xml"
python3 "${script_dir}/sdk_qualification.py" \
    --repo-root "${repo_root}" \
    --build-dir "${cmake_build_dir}" \
    --revision "${revision}" \
    --tests "${run_tests}" \
    --output "${evidence_path}"

if [[ "${run_tests}" == "ON" ]]; then
    ctest --test-dir "${cmake_build_dir}" --output-on-failure \
        --output-junit "${ctest_report}"
fi

stage_dir="$(mktemp -d "${sdk_store_dir}/.stage.XXXXXX")"

cleanup() {
    if [[ -n "${stage_dir}" && -d "${stage_dir}" ]]; then
        if [[ "${stage_dir}" == "${sdk_store_dir}/.stage."* ]]; then
            rm -rf -- "${stage_dir}"
        else
            printf 'Refusing to clean an unexpected SDK stage path: %s\n' \
                "${stage_dir}" >&2
        fi
    fi
}
trap cleanup EXIT

cmake --install "${cmake_build_dir}" --prefix "${stage_dir}"

python3 "${script_dir}/build-docs.py" \
    --repo-root "${repo_root}" \
    --output "${stage_dir}/docs" \
    --revision "${revision}"
package_arguments=(
    --repo-root "${repo_root}"
    --stage-dir "${stage_dir}"
    --revision "${revision}"
    --build-type "${build_type}"
    --llvm "${enable_llvm}"
    --qualification "${qualification}"
    --build-evidence "${evidence_path}"
)
if [[ "${run_tests}" == "ON" ]]; then
    package_arguments+=(--ctest-report "${ctest_report}")
fi
if [[ "${allow_dirty_source}" == "ON" ]]; then
    package_arguments+=(--allow-dirty-source)
fi
python3 "${script_dir}/package-sdk.py" \
    "${package_arguments[@]}"
validation_arguments=(--expected-revision "${revision}")
if [[ "${qualification}" == "rc-checks" ]]; then
    validation_arguments+=(--require-rc-checks)
fi
python3 "${script_dir}/validate-sdk.py" "${stage_dir}" "${validation_arguments[@]}"
if [[ "${qualification}" == "rc-checks" ]]; then
    python3 "${script_dir}/check-sdk-routes.py" \
        --sdk "${stage_dir}" \
        --expected-revision "${revision}" \
        --require-rc-checks \
        --output "${cmake_build_dir}/sdk-consumer-routes.json"
fi
python3 "${script_dir}/publish-sdk.py" \
    --stage-dir "${stage_dir}" \
    --sdk-dir "${sdk_dir}" \
    --store-dir "${sdk_store_dir}" \
    --validator "${script_dir}/validate-sdk.py"
stage_dir=""
trap - EXIT

python3 "${script_dir}/validate-sdk.py" "${sdk_dir}" "${validation_arguments[@]}"

printf 'NIL Framework SDK ready at %s\n' "${sdk_dir}"
printf 'Technical SDK qualification: %s (not a language release declaration)\n' "${qualification}"
printf 'Binaries are at the SDK root: %s\n' "${sdk_dir}"
printf 'Compiled standard library: %s\n' "${sdk_dir}/nillib"
printf 'Standard-library source: %s\n' "${sdk_dir}/nilstd"
printf 'HTML documentation: %s\n' "${sdk_dir}/docs"
