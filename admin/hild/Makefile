hild_home = /home/hild

dot_files = forward
sources = $(addprefix ., ${dot_files})
targets = $(addprefix ${hild_home}/, ${sources})

install: ${targets}

${hild_home}/.%: %
	install $^ $@

