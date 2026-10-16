module string_utils

@interop("winplat")
function _substring(s: text, start: i32, length: i32) -> text
end function

@interop("winplat")
function _string_length(s: text) -> i32
end function

pub function substring(s: text, start_index: i32, end_index: i32) -> text
    let mut si = start_index
    let mut ei = end_index
    if si < 0 then
        si = 0
    end if
    let len = length(s)
    if ei > len then
        ei = len
    end if
    if ei < si then
        return ""
    end if

    if si >= len then
        return ""
    end if

    return _substring(s, si, ei - si)
end function

pub function length(s: text) -> i32
    return _string_length(s)
end function

@interop("winplat")
function _char_at(s: text, index: i32) -> text
end function

pub function char_at(s: text, index: i32) -> text
    if index < 0 or index >= length(s) then
        return ""
    end if
    return _char_at(s, index)
end function

pub function index_of(s: text, needle: text) -> i32
    let s_len = length(s)
    let n_len = length(needle)
    if n_len == 0 then return 0 end if
    if n_len > s_len then return -1 end if
    let mut i = 0
    while i <= s_len - n_len do
        let sub = substring(s, i, i + n_len)
        if sub == needle then
            return i
        end if
        i = i + 1
    end while
    return -1
end function

pub function contains(s: text, needle: text) -> bool
    return index_of(s, needle) >= 0
end function

pub function trim(s: text) -> text
    let len = length(s)
    if len == 0 then return "" end if
    let mut start = 0
    while start < len do
        let c = char_at(s, start)
        if c != " " and c != "\t" and c != "\n" and c != "\r" then
            break
        end if
        start = start + 1
    end while
    let mut end_idx = len
    while end_idx > start do
        let c = char_at(s, end_idx - 1)
        if c != " " and c != "\t" and c != "\n" and c != "\r" then
            break
        end if
        end_idx = end_idx - 1
    end while
    return substring(s, start, end_idx)
end function

pub struct split_result
    items: array<text>
    count: i32
end struct

pub function split(s: text, delimiter: text) -> split_result
    let mut result = split_result { items: [], count: 0 }
    let s_len = length(s)
    let d_len = length(delimiter)
    if s_len == 0 then return result end if
    if d_len == 0 then
        result.items = [s]
        result.count = 1
        return result
    end if
    let mut start = 0
    let mut i = 0
    while i <= s_len - d_len do
        let sub = substring(s, i, i + d_len)
        if sub == delimiter then
            let part = substring(s, start, i)
            result.items = result.items + [part]
            result.count = result.count + 1
            i = i + d_len
            start = i
        else
            i = i + 1
        end if
    end while
    let last_part = substring(s, start, s_len)
    result.items = result.items + [last_part]
    result.count = result.count + 1
    return result
end function

pub function starts_with(s: text, prefix: text) -> bool
    let p_len = length(prefix)
    if p_len > length(s) then return false end if
    return substring(s, 0, p_len) == prefix
end function

pub function ends_with(s: text, suffix: text) -> bool
    let s_len = length(s)
    let sf_len = length(suffix)
    if sf_len > s_len then return false end if
    return substring(s, s_len - sf_len, s_len) == suffix
end function

pub function to_upper(s: text) -> text
    let mut result = ""
    let len = length(s)
    let mut i = 0
    while i < len do
        let c = char_at(s, i)
        if c >= "a" and c <= "z" then
            let code = _char_code(c) - 32
            result = result + _char_from_code(code)
        else
            result = result + c
        end if
        i = i + 1
    end while
    return result
end function

pub function to_lower(s: text) -> text
    let mut result = ""
    let len = length(s)
    let mut i = 0
    while i < len do
        let c = char_at(s, i)
        if c >= "A" and c <= "Z" then
            let code = _char_code(c) + 32
            result = result + _char_from_code(code)
        else
            result = result + c
        end if
        i = i + 1
    end while
    return result
end function

@interop("winplat")
function _char_code(c: text) -> i32
end function

@interop("winplat")
function _char_from_code(code: i32) -> text
end function

pub function repeat_char(c: text, count: i32) -> text
    let mut result = ""
    let mut i = 0
    while i < count do
        result = result + c
        i = i + 1
    end while
    return result
end function

pub function pad_right(s: text, total_width: i32, pad_char: text) -> text
    let len = length(s)
    if len >= total_width then return s end if
    return s + repeat_char(pad_char, total_width - len)
end function

pub function pad_left(s: text, total_width: i32, pad_char: text) -> text
    let len = length(s)
    if len >= total_width then return s end if
    return repeat_char(pad_char, total_width - len) + s
end function

@interop("winplat")
function _int_to_string(value: i32) -> text
end function

pub function int_to_text(value: i32) -> text
    # Pure-Ny fallback for values 0-255 (for token kinds)
    if value == 0 then return "0" end if
    if value == 1 then return "1" end if
    if value == 2 then return "2" end if
    if value == 3 then return "3" end if
    if value == 4 then return "4" end if
    if value == 5 then return "5" end if
    if value == 6 then return "6" end if
    if value == 7 then return "7" end if
    if value == 8 then return "8" end if
    if value == 9 then return "9" end if
    if value == 10 then return "10" end if
    if value == 11 then return "11" end if
    if value == 12 then return "12" end if
    if value == 13 then return "13" end if
    if value == 14 then return "14" end if
    if value == 15 then return "15" end if
    if value == 16 then return "16" end if
    if value == 17 then return "17" end if
    if value == 18 then return "18" end if
    if value == 19 then return "19" end if
    if value == 20 then return "20" end if
    if value == 21 then return "21" end if
    if value == 22 then return "22" end if
    if value == 23 then return "23" end if
    if value == 24 then return "24" end if
    if value == 25 then return "25" end if
    if value == 26 then return "26" end if
    if value == 27 then return "27" end if
    if value == 28 then return "28" end if
    if value == 29 then return "29" end if
    if value == 30 then return "30" end if
    if value == 31 then return "31" end if
    if value == 32 then return "32" end if
    if value == 33 then return "33" end if
    if value == 34 then return "34" end if
    if value == 35 then return "35" end if
    if value == 36 then return "36" end if
    if value == 37 then return "37" end if
    if value == 38 then return "38" end if
    if value == 39 then return "39" end if
    if value == 40 then return "40" end if
    if value == 41 then return "41" end if
    if value == 42 then return "42" end if
    if value == 43 then return "43" end if
    if value == 44 then return "44" end if
    if value == 45 then return "45" end if
    if value == 46 then return "46" end if
    if value == 47 then return "47" end if
    if value == 48 then return "48" end if
    if value == 49 then return "49" end if
    if value == 50 then return "50" end if
    if value == 51 then return "51" end if
    if value == 52 then return "52" end if
    if value == 53 then return "53" end if
    if value == 54 then return "54" end if
    if value == 55 then return "55" end if
    if value == 56 then return "56" end if
    if value == 57 then return "57" end if
    if value == 58 then return "58" end if
    if value == 59 then return "59" end if
    if value == 60 then return "60" end if
    if value == 61 then return "61" end if
    if value == 62 then return "62" end if
    if value == 63 then return "63" end if
    if value == 64 then return "64" end if
    if value == 65 then return "65" end if
    if value == 66 then return "66" end if
    if value == 67 then return "67" end if
    if value == 68 then return "68" end if
    if value == 69 then return "69" end if
    if value == 70 then return "70" end if
    if value == 71 then return "71" end if
    if value == 72 then return "72" end if
    if value == 73 then return "73" end if
    if value == 74 then return "74" end if
    if value == 75 then return "75" end if
    if value == 76 then return "76" end if
    if value == 77 then return "77" end if
    if value == 78 then return "78" end if
    if value == 79 then return "79" end if
    if value == 80 then return "80" end if
    if value == 81 then return "81" end if
    if value == 82 then return "82" end if
    if value == 83 then return "83" end if
    if value == 84 then return "84" end if
    if value == 85 then return "85" end if
    if value == 86 then return "86" end if
    if value == 87 then return "87" end if
    if value == 88 then return "88" end if
    if value == 89 then return "89" end if
    if value == 90 then return "90" end if
    if value == 91 then return "91" end if
    if value == 92 then return "92" end if
    if value == 93 then return "93" end if
    if value == 94 then return "94" end if
    if value == 95 then return "95" end if
    if value == 96 then return "96" end if
    if value == 97 then return "97" end if
    if value == 98 then return "98" end if
    if value == 99 then return "99" end if
    if value == 100 then return "100" end if
    if value == 101 then return "101" end if
    if value == 102 then return "102" end if
    if value == 103 then return "103" end if
    if value == 104 then return "104" end if
    if value == 105 then return "105" end if
    if value == 106 then return "106" end if
    if value == 107 then return "107" end if
    if value == 108 then return "108" end if
    if value == 109 then return "109" end if
    if value == 110 then return "110" end if
    if value == 111 then return "111" end if
    if value == 112 then return "112" end if
    if value == 113 then return "113" end if
    if value == 114 then return "114" end if
    if value == 115 then return "115" end if
    if value == 116 then return "116" end if
    if value == 117 then return "117" end if
    if value == 118 then return "118" end if
    if value == 119 then return "119" end if
    if value == 120 then return "120" end if
    if value == 121 then return "121" end if
    if value == 122 then return "122" end if
    if value == 123 then return "123" end if
    if value == 124 then return "124" end if
    if value == 125 then return "125" end if
    if value == 126 then return "126" end if
    if value == 127 then return "127" end if
    if value == 128 then return "128" end if
    if value == 129 then return "129" end if
    if value == 130 then return "130" end if
    if value == 131 then return "131" end if
    if value == 132 then return "132" end if
    if value == 133 then return "133" end if
    if value == 134 then return "134" end if
    if value == 135 then return "135" end if
    if value == 136 then return "136" end if
    if value == 137 then return "137" end if
    if value == 138 then return "138" end if
    if value == 139 then return "139" end if
    if value == 140 then return "140" end if
    if value == 141 then return "141" end if
    if value == 142 then return "142" end if
    if value == 143 then return "143" end if
    if value == 144 then return "144" end if
    if value == 145 then return "145" end if
    if value == 146 then return "146" end if
    if value == 147 then return "147" end if
    if value == 148 then return "148" end if
    if value == 149 then return "149" end if
    if value == 150 then return "150" end if
    if value == 151 then return "151" end if
    if value == 152 then return "152" end if
    if value == 153 then return "153" end if
    if value == 154 then return "154" end if
    if value == 155 then return "155" end if
    if value == 156 then return "156" end if
    if value == 157 then return "157" end if
    if value == 158 then return "158" end if
    if value == 159 then return "159" end if
    if value == 160 then return "160" end if
    if value == 161 then return "161" end if
    if value == 162 then return "162" end if
    if value == 163 then return "163" end if
    if value == 164 then return "164" end if
    if value == 165 then return "165" end if
    if value == 166 then return "166" end if
    if value == 167 then return "167" end if
    if value == 168 then return "168" end if
    if value == 169 then return "169" end if
    if value == 170 then return "170" end if
    if value == 171 then return "171" end if
    if value == 172 then return "172" end if
    if value == 173 then return "173" end if
    if value == 174 then return "174" end if
    if value == 175 then return "175" end if
    if value == 176 then return "176" end if
    if value == 177 then return "177" end if
    if value == 178 then return "178" end if
    if value == 179 then return "179" end if
    if value == 180 then return "180" end if
    if value == 181 then return "181" end if
    if value == 182 then return "182" end if
    if value == 183 then return "183" end if
    if value == 184 then return "184" end if
    if value == 185 then return "185" end if
    if value == 186 then return "186" end if
    if value == 187 then return "187" end if
    if value == 188 then return "188" end if
    if value == 189 then return "189" end if
    if value == 190 then return "190" end if
    if value == 191 then return "191" end if
    if value == 192 then return "192" end if
    if value == 193 then return "193" end if
    if value == 194 then return "194" end if
    if value == 195 then return "195" end if
    if value == 196 then return "196" end if
    if value == 197 then return "197" end if
    if value == 198 then return "198" end if
    if value == 199 then return "199" end if
    if value == 200 then return "200" end if
    if value == 201 then return "201" end if
    if value == 202 then return "202" end if
    if value == 203 then return "203" end if
    if value == 204 then return "204" end if
    if value == 205 then return "205" end if
    if value == 206 then return "206" end if
    if value == 207 then return "207" end if
    if value == 208 then return "208" end if
    if value == 209 then return "209" end if
    if value == 210 then return "210" end if
    if value == 211 then return "211" end if
    if value == 212 then return "212" end if
    if value == 213 then return "213" end if
    if value == 214 then return "214" end if
    if value == 215 then return "215" end if
    if value == 216 then return "216" end if
    if value == 217 then return "217" end if
    if value == 218 then return "218" end if
    if value == 219 then return "219" end if
    if value == 220 then return "220" end if
    if value == 221 then return "221" end if
    if value == 222 then return "222" end if
    if value == 223 then return "223" end if
    if value == 224 then return "224" end if
    if value == 225 then return "225" end if
    if value == 226 then return "226" end if
    if value == 227 then return "227" end if
    if value == 228 then return "228" end if
    if value == 229 then return "229" end if
    if value == 230 then return "230" end if
    if value == 231 then return "231" end if
    if value == 232 then return "232" end if
    if value == 233 then return "233" end if
    if value == 234 then return "234" end if
    if value == 235 then return "235" end if
    if value == 236 then return "236" end if
    if value == 237 then return "237" end if
    if value == 238 then return "238" end if
    if value == 239 then return "239" end if
    if value == 240 then return "240" end if
    if value == 241 then return "241" end if
    if value == 242 then return "242" end if
    if value == 243 then return "243" end if
    if value == 244 then return "244" end if
    if value == 245 then return "245" end if
    if value == 246 then return "246" end if
    if value == 247 then return "247" end if
    if value == 248 then return "248" end if
    if value == 249 then return "249" end if
    if value == 250 then return "250" end if
    if value == 251 then return "251" end if
    if value == 252 then return "252" end if
    if value == 253 then return "253" end if
    if value == 254 then return "254" end if
    if value == 255 then return "255" end if
    # Fallback to FFI for larger values
    return _int_to_string(value)
end function

pub function char_code(c: text) -> i32
    return _char_code(c)
end function

pub function char_from_code(code: i32) -> text
    return _char_from_code(code)
end function

pub function text_to_int(s: text) -> i32
    let trimmed = trim(s)
    let len = length(trimmed)
    if len == 0 then return 0 end if
    let mut result = 0
    let mut negative = false
    let mut start = 0
    if char_at(trimmed, 0) == "-" then
        negative = true
        start = 1
    end if
    let mut i = start
    while i < len do
        let c = char_at(trimmed, i)
        let code = _char_code(c)
        if code >= 48 and code <= 57 then
            result = result * 10 + (code - 48)
        else
            break
        end if
        i = i + 1
    end while
    if negative then result = 0 - result end if
    return result
end function

pub function concat(s1: text, s2: text) -> text
    return s1 + s2
end function

pub function len(s: text) -> i32
    return length(s)
end function
