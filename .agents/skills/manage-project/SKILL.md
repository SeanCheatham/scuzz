---
name: manage-project
description: Project and task management. Used for short and long-term planning.
---

# Manage Project

## When to Use
- When the user doesn't know what to work on next
- When the user wants to change directions with the project
- Whenever a feature changes or a significant change is made

## Instructions
- Use `docs/plans.md` to describe short-term plans. When implementing plans, be sure to update the status of the doc. Unless explicitly gitignored, be sure to commit this with other changes. If there is no short-term plan yet, this file may not exist.
- Use `docs/vision.md` to describe long-term goals or arcs for the project. This should guide `docs/plans.md` when there is no next task. Keep this up-todate and checked into git unless explicitly gitignored.
- Use `docs/gaps.md` to describe unknowns or major milestones that must be met at some point. It can also guide `docs/plans.md`. This should also be checked into git unless explicitly gitignored.
- When invoked by the user, ensure plans.md is up-to-date and then implement it. When entirely completed, remove plans.md. If partially complete, update plans.md with the status accordingly.

In general, be sure to keep the project on-track. Avoid documentation bloat. Do NOT keep historical planning information; once something is implemented, remove any references to it in planning unless it's necessary information for future plans.
