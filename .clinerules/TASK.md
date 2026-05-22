# Rule of plan execution

## After switching to ACT mode, append the plan to the `.tasklog/<taskid>.md` file

- Always add first step to any plan **After switching to ACT mode, appent the plan to the `.tasklog/<taskid>.md`.**
- Until the work plan is finally approved and switched to ACT mode, only reading of the data for analysis and plan development is permitted; editing or changing the data is prohibited.
- Always append data to the `.tasklog/<taskid>.md` file. The file may be modified by other tools, so it **needs to be re-read** each time new data is added.

## Record all actions to `.tasklog/<taskid>.md`

- **After switching to ACT mode, append the plan to the `.tasklog/<taskid>.md`.**
- Always append the results **of each step of the plan tasks** in the `.tasklog/<taskid>.md` file, including any negative results.
- After **several unsuccessful** attempts to solve a problem, **stop and analyze the problems**.
- If the problem persists and requires a change in the architecture or the solution is outside the scope of the approved plan, **stop task execution** and provide a report on the problem encountered with several proposed solutions to choose from.

## Structure your prompt for efficient LLM context caching.

- Place static content (`AGENTS.md`, `ARCH.md`, etc.) first — they form a cacheable prefix.
- Place dynamic content (`.tasklog/<taskid>.md`, current file, command results) after static content.
- When updating `.tasklog/<taskid>.md`, send it at the start of a new turn, not mid-conversation.
- In long sessions, periodically re-send static blocks (`AGENTS.md`, `ARCH.md`, etc.) to prevent cache eviction.

## Before plan complete and stop task execution

- Re-Read `.tasklog/<taskid>.md` and review the original plan for completeness and append any items not completed and the reasons why they were not completed.
- Systematize the errors that occurred during the tasks plan and suggest improvements to prevent them in the future.
- **Append all results** of the plan execution in the `.tasklog/<taskid>.md` file with a brief description of the results.
