export interface TextOffsets { start: number; end: number; }

// Compiler locations count UTF-8 bytes; VS Code offsets count UTF-16 units.
// Interior scalar bytes must never become approximate navigation or edits.
export function createByteRangeMapper(text: string): (start: number, length: number) => TextOffsets | undefined {
  const boundaries = new Uint32Array(Buffer.byteLength(text, "utf8") + 1);
  let bytes = 0;
  let units = 0;
  boundaries[0] = 1;
  for (const scalar of text) {
    bytes += Buffer.byteLength(scalar, "utf8");
    units += scalar.length;
    boundaries[bytes] = units + 1;
  }
  return (start, length) => {
    if (!Number.isSafeInteger(start) || !Number.isSafeInteger(length) || start < 0 || length < 0 ||
        !Number.isSafeInteger(start + length) || start + length >= boundaries.length) return undefined;
    const first = boundaries[start];
    const last = boundaries[start + length];
    return first && last ? { start: first - 1, end: last - 1 } : undefined;
  };
}

export function byteRangeToOffsets(text: string, start: number, length: number): TextOffsets | undefined {
  return createByteRangeMapper(text)(start, length);
}
