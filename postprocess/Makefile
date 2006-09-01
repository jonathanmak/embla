all:

# Remove make's bloody builtin rules
.SUFFIXES:
%: %.o
%: %.c
%.o: %.c


CC = gcc

examples = ex1 ex2 ex3 ex4 ex5 ex7 # qsorter
depgraphs = $(addsuffix .depgraph.tex, ${examples})
deplists = $(addsuffix .deplist.tex, ${examples} qsorter)

all: ${depgraphs} ${deplists}

%.depgraph.tex: depgraph.awk %.c %.trace
	awk -f $^ > $@

%.deplist.tex: deplist.awk %.c %.trace
	awk -f $^ > $@

# Make won't allow % among the dependencies, so make a recursive call...
%.trace: sorttrace %.o
	${MAKE} $*
	rm -f embla.trace
	[ -f "${VALGRIND}" ] || ( echo "Set VALGRIND variable" ; exit 1 )
	${VALGRIND} --tool=embla ./$*
	./sorttrace embla.trace > $@
	rm embla.trace

%: %.o
	${CC} --static -O0 -g -o $@ $^

%.o: %.c
	${CC} -O0 -g -c -o $@ $<

copy-doc: all
	for f in ${depgraphs} ${deplists} ; do \
	  install $$f ../firstpaper || exit 1 ; \
	done

clean:
	rm -f ${examples}
	rm -f $(addsuffix .o, ${examples})
	rm -f ${deplists}
	rm -f ${depgraphs}
	rm -f $(addsuffix .trace, ${examples})
	rm -f embla.trace
	rm -f $(addsuffix .ps, ${dotfiles})
	rm -f $(addsuffix .dot, ${dotfiles})

.SECONDARY:
