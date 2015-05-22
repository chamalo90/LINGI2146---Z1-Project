CONTIKI_PROJECT=border-router
all: $(CONTIKI_PROJECT) 

CONTIKI=../../contiki

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

WITH_UIP6=1
UIP_CONF_IPV6=1
CFLAGS+= -DUIP_CONF_IPV6_=1
CFLAGS+= -DUIP_CONF_IPV6_RPL=1
#linker optimizations
SMALL=1

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
PROJECT_SOURCEFILES += slip-bridge.c

# REST Engine shall use Erbium CoAP implementation
APPS += er-coap
APPS += rest-engine


ifeq ($(PREFIX),)
 PREFIX = aaaa::1/64
endif

CONTIKI_WITH_IPV6 = 1
include $(CONTIKI)/Makefile.include

$(CONTIKI)/tools/tunslip6:	$(CONTIKI)/tools/tunslip6.c
	(cd $(CONTIKI)/tools && $(MAKE) tunslip6)

connect-router:	$(CONTIKI)/tools/tunslip6
	sudo $(CONTIKI)/tools/tunslip6 $(PREFIX)

connect-router-cooja:	$(CONTIKI)/tools/tunslip6
	sudo $(CONTIKI)/tools/tunslip6 -a 127.0.0.1 $(PREFIX)
