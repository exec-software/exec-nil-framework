import nil.std.markup.escaping

# A component: an ordinary function returning a document, so that composing
# pages does not go back through text.
fn card(title: text, count: i32) -> document
    return $markup
        <section class="card">
          <h2>{title}</h2>
          <p>{count} items</p>
        </section>
    end markup
end fn

fn page(signed_in: bool, who: text, names: array<text>, query: text, target: text) -> document
    return $markup
        <html>
          <head><title>Report &amp; Review</title></head>
          <body>
            {card("Pro & Con", names.len() as i32)}
            <nav>
              {if signed_in}
                <a href="/logout">Log out, {who}</a>
              {elseif who == "guest"}
                <a href="/register">Register</a>
              {else}
                <a href="/login">Log in</a>
              {end if}
            </nav>
            <ul class="names">
              {for name in names}
                <li class="{name}" data-total="{names.len()}">{name}</li>
              {end for}
            </ul>
            <a href="{target}">target</a>
            <a href="/search?q={query}">search</a>
            <br/>
            <img src="/logo.png">
            <!-- comments survive -->
            <style>body { margin: 0 }</style>
            <script>{raw "var ready = 1 < 2;"}</script>
            <p>{{literal braces}} and {raw "<em>trusted</em>"}</p>
          </body>
        </html>
    end markup
end fn

fn main() -> i32
    let names = ["alpha", "b<ad>", "gamma"]
    println(nil.std.markup::render(page(true, "Na<me>", names, "a b&c", "/dashboard")))
    let empty: array<text> = []
    println(nil.std.markup::render(page(false, "guest", empty, "x", "javascript:alert(1)")))
    println(nil.std.markup::render(nil.std.markup::empty()))
    return 0
end fn
