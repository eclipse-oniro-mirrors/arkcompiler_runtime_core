VENV = .venv
VENV_SYMLINK = venv # to ensure correct work of the IDEs
VENV_PYTHON = $(VENV)/bin/python
SYSTEM_PYTHON = $(or $(shell which python3.11), $(shell which python3))
PYTHON = $(or $(wildcard $(VENV_PYTHON)), $(SYSTEM_PYTHON))
PYTHON_FILES = test src run.py lib.py record_hdc.py execute_flow.py create_view.py compare.py

test:
	$(PYTHON) -m unittest discover -v -s test -p "*_test.py"
.PHONY: test

mypy:
	MYPYPATH=. $(PYTHON) -m mypy . --explicit-package-bases
.PHONY: mypy

autopep8:
	find $(PYTHON_FILES) -type f -name "*.py" | xargs $(PYTHON) -m autopep8 -i
.PHONY: autopep8

pylint:
	$(PYTHON) -m pylint --disable=all --enable=too-many-nested-blocks,wrong-import-order,protected-access --max-nested-blocks=4 --score=n $(PYTHON_FILES)
.PHONY: pylint

tests_full:
	make autopep8
	make pylint
	make mypy
	make test
.PHONY: tests_full

venv:
	rm -rf $(VENV) $(VENV_SYMLINK)
	$(SYSTEM_PYTHON) -m venv $(VENV)
	$(VENV_PYTHON) -m pip install -r requirements.txt
	ln -s $(VENV) $(VENV_SYMLINK)
.PHONY: venv
