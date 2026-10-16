module editor.fixture

@ai_context(
    intent="Exercise the VS Code grammar",
    assumptions=["Text is UTF-8"],
    constraints=["Keep the example small"]
)
pub fn greet(name: text) -> text
    return "Hello, " + name
end fn

test "greets a user"
    assert.equal(greet("Ny"), "Hello, Ny")
end test
