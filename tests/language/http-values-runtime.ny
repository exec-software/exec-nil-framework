import ny.lang.net

fn main() -> i32
    let parsed: Option<http_method> = http_method.parse("pOsT")
    println(parsed.is_some())
    println(parsed.unwrap().to_text())
    println(http_method.parse("TRACE").is_none())
    let method_get: http_method = http_method.get
    let method_put: http_method = http_method.put
    let method_delete: http_method = http_method.delete
    let method_patch: http_method = http_method.patch
    let method_head: http_method = http_method.head
    let method_options: http_method = http_method.options
    println(method_get.to_text())
    println(method_put.to_text())
    println(method_delete.to_text())
    println(method_patch.to_text())
    println(method_head.to_text())
    println(method_options.to_text())

    let ok: http_status = http_status.ok
    let no_content: http_status = http_status.no_content
    let found: http_status = http_status.found
    let client_error_status: http_status = http_status.not_found
    let bad_gateway: http_status = http_status.bad_gateway
    println(ok.code())
    println(no_content.is_success())
    println(found.is_redirect())
    println(client_error_status.is_client_error())
    println(bad_gateway.is_server_error())
    let method_not_allowed: http_status = http_status.from_code(405).unwrap()
    println(method_not_allowed.code())
    println(http_status.from_code(418).is_none())

    let request: http_request = http_request.post("https://example.test/items", "payload")
        .with_header("content-type", "text/plain")
        .with_content_type("application/json")
        .with_bearer_token("token")
    println(request.method().to_text())
    println(request.url())
    println(request.body().unwrap())
    println(request.header("CONTENT-TYPE").unwrap())
    println(request.header("authorization").unwrap())
    println(request.header("missing").is_none())

    let get_request: http_request = http_request.get("https://example.test/health")
    println(get_request.method().to_text())
    println(get_request.body().is_none())

    let response: http_response = http_response.new(http_status.created, "created")
        .with_header("X-Request-ID", "first")
        .with_header("x-request-id", "second")
    println(response.status().code())
    println(response.body())
    println(response.is_ok())
    println(response.header("X-REQUEST-ID").unwrap())
    return 0
end fn
