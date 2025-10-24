.PHONY: install install-env format

ENV_NAME = CO2map
PYTHON_VERSION = 3.13
PACKAGE = .

install:
	@echo "Checking for existing environment: $(ENV_NAME)"
	@if conda info --envs | grep -q "^$(ENV_NAME) "; then \
		echo "Removing existing environment: $(ENV_NAME)"; \
		conda remove -y --name $(ENV_NAME) --all; \
	fi
	@echo "Creating new environment: $(ENV_NAME)"
	conda create -y -n $(ENV_NAME) python=$(PYTHON_VERSION) matplotlib numpy scipy pandas scikit-learn jupyterlab ipykernel
	@echo "Installing package in editable mode in $(ENV_NAME)"
	conda run -n $(ENV_NAME) pip install -e .[dev]
	@echo "Done. To activate the environment, run: conda activate $(ENV_NAME)"

format:
	black $(PACKAGE)