include $(top_srcdir)/Makefile.tool.am

noinst_PROGRAMS = 
if VGP_X86_LINUX
noinst_PROGRAMS += embla-x86-linux
endif
if VGP_AMD64_LINUX
noinst_PROGRAMS += embla-amd64-linux
endif
if VGP_PPC32_LINUX
noinst_PROGRAMS += embla-ppc32-linux
endif

EMBLA_SOURCES_COMMON = em_main.c

embla_x86_linux_SOURCES      = $(EMBLA_SOURCES_COMMON)
embla_x86_linux_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
embla_x86_linux_CFLAGS       = $(AM_CFLAGS_X86_LINUX)
embla_x86_linux_DEPENDENCIES = $(COREGRIND_LIBS_X86_LINUX)
embla_x86_linux_LDADD        = $(TOOL_LDADD_X86_LINUX)
embla_x86_linux_LDFLAGS      = $(TOOL_LDFLAGS_X86_LINUX)

embla_amd64_linux_SOURCES      = $(EMBLA_SOURCES_COMMON)
embla_amd64_linux_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
embla_amd64_linux_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX)
embla_amd64_linux_DEPENDENCIES = $(COREGRIND_LIBS_AMD64_LINUX)
embla_amd64_linux_LDADD        = $(TOOL_LDADD_AMD64_LINUX)
embla_amd64_linux_LDFLAGS      = $(TOOL_LDFLAGS_AMD64_LINUX)

embla_ppc32_linux_SOURCES      = $(EMBLA_SOURCES_COMMON)
embla_ppc32_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
embla_ppc32_linux_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX)
embla_ppc32_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_LINUX)
embla_ppc32_linux_LDADD        = $(TOOL_LDADD_PPC32_LINUX)
embla_ppc32_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_LINUX)
