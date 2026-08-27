<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2026 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# AI assistants

Open-lmake has a *skill* that teaches AI coding assistants how to write good Lmakefiles:
the [skills/lmakefile](https://github.com/cesar-douady/open-lmake/tree/main/skills/lmakefile) directory of the repository.

A skill is a directory of plain markdown files following the [Agent Skills](https://agentskills.io/) format:
an entry point `SKILL.md` (small, ~2k tokens) that the agent loads whenever the task involves an Lmakefile,
plus reference files under `references/` that it loads on demand.
It covers the open-lmake paradigm, rule authoring, the classic pitfalls LLMs fall into,
integrating stateful tools (conan, pip, cargo, ...) and how to verify a flow with `lmake`/`lshow`.

The skill is written for humans as much as for agents.

## Claude Code

The open-lmake repository is also a Claude Code plugin marketplace, so installation is 2 commands:

```
/plugin marketplace add cesar-douady/open-lmake
/plugin install lmakefile@open-lmake
```

Alternatively, skip the plugin system and copy (or symlink) the skill directory:

```
cp -r skills/lmakefile ~/.claude/skills/           # available in all your projects
cp -r skills/lmakefile <project>/.claude/skills/   # available in one project (and to your co-workers if committed)
```

Either way there is nothing to activate : Claude loads the skill by itself when the task touches an Lmakefile.

## claude.ai and the Claude API

On [claude.ai](https://claude.ai), custom skills are uploaded as a zip file in the settings (capabilities section):

```
cd skills ; zip -r lmakefile.zip lmakefile
```

With the Claude API, upload the same zip through the
[Skills API](https://platform.claude.com/docs/en/agents-and-tools/agent-skills/overview) and attach it to your requests.

## Other agents (Codex, Cursor, Gemini CLI, ...)

Nothing in the skill is Claude-specific : it is plain markdown plus a YAML header.
A growing number of tools understand the Agent Skills format natively;
for those, copy `skills/lmakefile` into the tool's skill directory (check its documentation for the location) and you are done.

For any other tool, reproduce manually what skill-aware tools do (a small entry in the permanent context, details on demand):

1. Copy `skills/lmakefile/` into your project (or anywhere the agent can read).
2. Add to the instructions file your tool always reads (`AGENTS.md`, `GEMINI.md`, `.cursor/rules`, a system prompt, ...):

> When writing, reviewing or debugging an Lmakefile, first read `skills/lmakefile/SKILL.md` and follow it.
> Load the files under `skills/lmakefile/references/` on demand, as `SKILL.md` directs.

Only the entry point is paid for up front; the references are read when needed.
