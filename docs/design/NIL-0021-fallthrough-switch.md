# NIL-0021: Switch statements and expressions with automatic fallthrough

**Status:** Accepted; initial literal and payload-free enum selector profile.

## Context

Ny `match` and Matrix `consider` select independent pattern arms. Some programs
need a dispatch point followed by several consecutive stages, including stages
shared by multiple labels. A separate switch construct expresses that control
flow directly while preserving the existing pattern-matching contract.

## Decision

Both frontends lower switch statements and expressions to the shared compiler
representation. The selector is evaluated once. Dispatch compares checked
integral, boolean, text, or Ny character literal labels for equality, entering
the matching case or the default. Qualified variants of payload-free enums
(Matrix choices) are also supported. The entire enum must be payload-free.
Floating-point selectors, duplicate labels, and multiple defaults are rejected.
Labels do not execute code or bind variables.

Execution continues through subsequent case bodies in source order until an
explicit exit. The default can appear anywhere and participates in this same
fallthrough order. A statement with no match and no default has no effect.
Each case introduces an independent lexical scope; falling through does not
make the preceding case's locals available.

Ny uses `switch selector`, `case literal:`, and `default:`, terminated by
`end switch` or enclosed in braces. A bare `break` targets the nearest loop or
switch. `break expression` yields a result only when that target is a switch
expression; an intervening loop makes the value-bearing exit invalid.

Matrix uses `switch selector`, `case literal then`, `otherwise`, and
`end switch`. `stop switching` targets the nearest switch and
`stop switching with expression` supplies its value. These explicit exits may
cross an intervening loop. `stop repeating` and `continue repeating` retain
their loop targets. Ny `continue` likewise ignores intervening statement
switches. Loop exits may not cross an active switch expression in this initial
profile: they would abandon the result computation and any surrounding pending
operands. The checker and lowering reject that transfer. Loops entirely inside
a case remain valid, as does Matrix yielding a switch result across an inner
loop.

A switch in value position must have a default and a compatible result on
every reachable normal exit. Numeric scalar results may widen according to
the existing type rules. Cases may fall into a later result-producing case;
they may also return from the function or raise a fault. Reaching the end or
taking a bare switch exit without a result is rejected. Matrix `give` in a
case remains a paragraph return, so it cannot be confused with the switch's
explicit result exit.

## Ownership and tooling

Case-local owners and borrows are cleaned up at case boundaries and transfers
of control. Movement of an outer owner across switch paths remains
conservatively rejected. Broader move-state joins require separate ownership
work rather than assumptions based on a fallthrough path.

Editor indexing visits nested value switches, including switches inside call
arguments and initializers. Case locals stay confined to their case scopes.
Completion and snippets expose both forms; syntax highlighting distinguishes
control words from similarly named members. Contextual surface words must not
silently invalidate ordinary member access.

## Qualification

Executable route fixtures cover dispatch, automatic fallthrough, defaults in
different positions, stacked labels, nested loops and switches, value joins,
and cleanup. Rejection fixtures cover duplicate labels, mismatched selectors,
missing results/defaults, invalid exits, and ownership restrictions. LSP tests
cover case-local visibility and nested expression traversal; grammar tests
cover highlighting, indentation, and snippets.

The [language reference](../language-reference/switch.md) defines the source
contract. Pattern arms, guards, payload destructuring, and executable case
labels remain the domain of the existing match/consider constructs or future
separate proposals.
