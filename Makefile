# Set HAVE_MBEDTLS variable if you want to use MBEDTLS instead of TOMCRYPT

OBJECTS_O = onvif_simple_server.o device_service.o media_service.o ptz_service.o events_service.o fault.o conf.o utils.o log.o ezxml_wrapper.o ezxml/ezxml.o
OBJECTS_N = onvif_notify_server.o conf.o utils.o log.o ezxml_wrapper.o ezxml/ezxml.o
OBJECTS_W = wsd_simple_server.o utils.o log.o ezxml_wrapper.o ezxml/ezxml.o
ifdef HAVE_MBEDTLS
INCLUDE = -DHAVE_MBEDTLS -I../mbedtls/include -ffunction-sections -fdata-sections -lrt
LIBS_O = -Wl,--gc-sections ../mbedtls/library/libmbedcrypto.a -lpthread -lrt
LIBS_N = -Wl,--gc-sections ../mbedtls/library/libmbedcrypto.a -lpthread -lrt
else
INCLUDE = -I../libtomcrypt/src/headers -ffunction-sections -fdata-sections -lrt
LIBS_O = -Wl,--gc-sections ../libtomcrypt/libtomcrypt.a -lpthread -lrt
LIBS_N = -Wl,--gc-sections ../libtomcrypt/libtomcrypt.a -lpthread -lrt
endif
LIBS_W = -Wl,--gc-sections

all: onvif_simple_server onvif_notify_server wsd_simple_server

log.o: log.c $(HEADERS)
	$(CC) -c $< -std=c99 -fPIC -Os $(INCLUDE) -o $@

%.o: %.c $(HEADERS)
	$(CC) -c $< -fPIC -Os $(INCLUDE) -o $@

onvif_simple_server: $(OBJECTS_O)
	$(CC) $(OBJECTS_O) $(LIBS_O) -fPIC -Os -o $@
	$(STRIP) $@

onvif_notify_server: $(OBJECTS_N)
	$(CC) $(OBJECTS_N) $(LIBS_N) -fPIC -Os -o $@
	$(STRIP) $@

wsd_simple_server: $(OBJECTS_W)
	$(CC) $(OBJECTS_W) $(LIBS_W) -fPIC -Os -o $@
	$(STRIP) $@

.PHONY: clean

clean:
	rm -f onvif_simple_server
	rm -f onvif_notify_server
	rm -f wsd_simple_server
	rm -f $(OBJECTS_O)
	rm -f $(OBJECTS_N)
	rm -f $(OBJECTS_W)
