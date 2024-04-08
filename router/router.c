#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ip.h"
#include "config.h"
#include "util.h"
#include "log.h"

/*
 * Add a forwarding entry to your forwarding table.  The network address is
 * given by the network and netlength arguments.  Datagrams destined for
 * this network are to be sent out on the indicated interface.
 */
void addForwardEntry(uint32_t network, int netlength, int interface) {
    // TODO: Implement this
}

/*
 * Add an interface to your router. 
 */
void addInterface(int interface) {
    // TODO: Implement this
}

void run() {
    int port = getDefaultPort();
    int fd = udp_open(port);
    // TODO: Implement this
}

int main(int argc, char **argv) {
    logConfig("router", "packet,error,failure");

    char *configFileName = "router.config";
    if (argc > 1) {
	configFileName = argv[1];
    }
    configLoad(configFileName, addForwardEntry, addInterface);
    run();
    return 0;
}
