	ash, a terminal AI agent

An AI agent takes a request in plain language, hands it to a model, and
the model works your tree with tools: shell, read, write, edit, grep.
And you see results colorized in a scrolling transcript. ash is such an
agent, written in C. the incumbents run on npm, and npm is garbage. The
naming clash with the 1989 Almquist shell may or may not be relevant,
but we're too young to remember that anyway.

Two ideas:

The agent loop drives model and tools and emits a typed event
stream. The TUI is one consumer, --rpc is another.

The framebuffer is the single writer to your tty. Widgets render into
cells, frames get diffed, the diff goes out as minimal VT. The mouse
wheel scrolls the transcript, why the prompt gives multiline editing,
clipboard, undo, and a selection across a soft-wrapped line copies
the logical text, character for character.

Chrome is a lua script you can replace or modify as you like.
