import ny.lang.db
import ny.lang.net

fn main() -> i32
    let connection = db_connection.connect("host=localhost")
    let client = http_client.new()
    let response = client.get("https://example.test")
    return 0
end fn
