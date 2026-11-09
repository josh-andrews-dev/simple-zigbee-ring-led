.PHONY: help private-check

help: ## List the commands
	@grep -hE '^[a-z][a-z0-9-]*:.*## ' $(MAKEFILE_LIST) | sort -u | awk -F':.*## ' '{printf "  %-18s %s\n", $$1, $$2}'

private-check: ## List private names in this repo's files, history and GitHub text (prints the exact words; needs the private list)
	@python3 scripts/standard/private_check.py local
