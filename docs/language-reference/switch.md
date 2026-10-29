# Switch

`switch` selects a case by equality, then executes consecutive case bodies
until an explicit exit or the end of the switch. Both Ny and Matrix provide
statement and value forms. Pattern matching remains available through Ny
`match` and Matrix `consider`, with their existing independent-arm behavior.

## Statements

```ny
let mut total = 0
switch level
    case 1:
        total = total + 1
    case 2:
        total = total + 2
        break
    default:
        total = 10
end switch
```

The equivalent Matrix source is:

```matrix
variable total is 0
switch level
    case 1 then
        set total to total plus 1
    case 2 then
        set total to total plus 2
        stop switching
    otherwise
        set total to 10
end switch
```

For `level` equal to 1, both case bodies run and `total` becomes 3. For 2,
execution begins at the second case and `total` becomes 2. Other values select
the default body. Ny also accepts braces around the cases:

```ny
switch level {
    case 1:
    case 2:
        println("low")
        break
    default:
        println("other")
}
```

The selector is evaluated once. Labels are checked integral, boolean, or text
literals compatible with the selector, or qualified variants of a payload-free
enum (a Matrix choice). Ny character literals are also supported. Signed
integral labels are supported; floating-point selectors and labels are not.
The entire enum/choice must be payload-free; a mixed choice is unsupported
even when the written cases name only variants without payloads. Labels do not
contain executable expressions, guards, or binding patterns. Duplicate labels
and multiple default bodies are errors.

A default body can appear anywhere. Dispatch chooses a matching case first;
only if there is no match does it select the default. After dispatch, execution
falls through in source order without testing later labels again, including
across the default body. With no matching case and no default, a statement
switch does nothing. An empty case can share the following case's behavior.

## Values

A switch expression can contain several statements before yielding a result:

```ny
let message = switch level {
    case 1:
    case 2:
        break "low"
    default:
        break "other"
}
```

```matrix
value message is switch level
    case 1 then
    case 2 then
        stop switching with "low"
    otherwise
        stop switching with "other"
end switch
```

Ny `break expression` and Matrix `stop switching with expression` evaluate and
yield that expression, then leave the switch. A value form requires a default,
even when boolean cases cover both values. Every reachable normal exit must
supply a result. Falling off the final body or taking a bare switch exit is an
error; falling into a later body that yields a result is valid. A function
return or `raise` can leave a path without producing a switch result.

At least one arm must yield a value to establish the result type.
Result types must be compatible. The normal scalar widening rules apply to
numeric results. A switch expression can be nested in another expression,
including another switch, a call argument, or a binding initializer.

Matrix `give` inside a switch case returns from the enclosing paragraph. It
does not yield a switch value. Use `stop switching with` for that purpose.

## Exit targets and scopes

| Operation | Ny | Matrix |
| --- | --- | --- |
| Leave a switch | `break`, if it is the nearest loop or switch | `stop switching` |
| Yield a switch value | `break value`, if a value switch is the nearest loop or switch | `stop switching with value` |
| Leave a loop | `break`, if it is the nearest loop or switch | `stop repeating` |
| Continue a loop | `continue` | `continue repeating` |

Ny `break value` cannot cross an intervening loop to reach a switch. Matrix's
explicit switch exit targets the nearest switch even across an intervening
loop; its loop exits continue to target the nearest loop. A switch does not
capture `continue` in either language.

The initial value-switch implementation rejects a loop exit that crosses a
switch expression: `continue`/`continue repeating` cannot leave a value switch
for an enclosing loop, and Matrix `stop repeating` cannot do so either. Such
an exit could abandon partially evaluated operands surrounding the switch.
Loops wholly inside a case can still break or continue normally. Matrix
`stop switching with` can still yield from a switch expression across an
inner loop, because the switch completes with a result.

Each case has its own lexical scope. A local declared in one case is not
visible in a later case, even when execution falls through. Declare shared
mutable state outside the switch. Local owners and borrows are cleaned up
when their case ends or exits. Moving an owner from an enclosing scope across
switch paths is conservatively rejected in this initial implementation.

See [NIL-0021](../design/NIL-0021-fallthrough-switch.md) for the shared design.
