hild_home = /home/hild

dot_files = crontab forward
sources = $(addprefix ., ${dot_files})
targets = $(addprefix ${hild_home}/, ${sources})

cron_files = $(wildcard cron/*/*.py) cron/run_cron.py

install: ${targets} cron_install
	crontab ${hild_home}/.crontab

${hild_home}/.%: %
	install $^ $@

cron_install:
	for f in ${cron_files}; do \
	  install -D $$f ${hild_home}/$$f ; \
	done
