# LLM Use Guidelines

After much experimenting I am reluctantly forced to admit that there is actually something to this and it can be a useful tool in the right hands. However, guidelines are necessary to ensure quality.

## Supported Tools

Agent instructions are provided for both **Claude Code** (CLAUDE.md) and **GitHub Copilot** (.github/copilot-instructions.md). Even when using Copilot, Claude Sonnet is the recommended model — Copilot's agent structure differs but the same instructions apply. So far Claude has demonstrated better capability than other models in intuitively understanding the design ideas behind cx.

## Documentation

Use of LLM tools to produce well-written documentation is encouraged. Documentation is one of the stronger use cases for these tools. All generated documentation must be double-checked for accuracy before committing — LLMs can produce fluent but incorrect descriptions of behavior, types, or semantics.

## Coding

Use of agents for coding tasks is less encouraged. CX is a low-level framework, and any mistake — no matter how minor — can result in instability or a complete program crash. That said, agents are acceptable for tasks that would be highly labor intensive to do by hand (e.g., mechanical transformations, boilerplate generation, repetitive refactoring), provided the output is carefully reviewed.

## Design

Use of agents for design discussions is permissible. A human must drive the process with clearly defined goals, outcomes, and requirements. Agents can help explore tradeoffs or articulate options, but design decisions remain the responsibility of the developer.

## Review and Commit Policy

- All agent output must be reviewed and manually approved before commit.
- **Manual approval mode is highly recommended** when using any agentic coding tool.
- **Fully automated commits to the repository are strictly forbidden.** A human must review every diff and explicitly approve each commit.