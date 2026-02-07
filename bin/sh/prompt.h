#ifndef PROMPT_H
#define PROMPT_H

/**
 * expand_prompt_escapes - Expands \! and other escapes in PS1.
 * @ps1: The raw prompt string.
 * @command_count: Current command history number.
 * 
 * Returns a newly allocated string with escapes expanded, or NULL on error.
 */
char *expand_prompt_escapes(const char *ps1, int command_count, int extended, int depth);

/**
 * evaluate_prompt - Fully evaluates prompt including variable expansion and command substitutions.
 * @ps1: The raw prompt string.
 * @command_count: Current command history number.
 * @extended: If non-zero, use zsh-style %escapes; otherwise use bash-style \escapes.
 * 
 * Returns a newly allocated string ready for display, or NULL on error.
 */
char *evaluate_prompt(const char *ps1, int command_count, int extended);

#endif
