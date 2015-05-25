# LINGI2146 - Z1-Project

## Contiki

Because we backported modules and fixed tiny bugs, we have to use Contiki's
`release-2-7` branch from our Github repository: https://github.com/ludov04/contiki

Then we can compile the two parts as usual Contiki projects.

## How to compile our project

This can be done with a few Unix commands.

First, download Contiki from our repository and create an empty dir:

    $ git clone -b release-2-7 https://github.com/ludov04/contiki.git
    $ mkdir LINGI2146

Extract our project inside this `LINGI2146` dir located next to the just created
`contiki` directory.
Then compile the border router part:

    $ cd "LINGI2146/Mote 1 - Border router"
    $ make border-router

Connect the first Z1 device and upload our program:

    $ sudo chown $USER:$USER /dev/ttyUSB0
    $ make MOTES=/dev/ttyUSB0 border-router.upload

Now compile the fan activator part:

    $ cd "../Mote 2 - Activator"
    $ make fan-activator

Connect the second Z1 device and upload our program:

    $ sudo chown $USER:$USER /dev/ttyUSB1
    $ make MOTES=/dev/ttyUSB1 border-router.upload


## CoAP

We can easily access to data from CoAP servers running on both motes.
You first need to be connected to the border router:

    $ make connect-router

Then you can visit this URL by using [Copper Mozilla Firefox extension](https://addons.mozilla.org/en-US/firefox/addon/copper-270430/):

    coap://[aaaa::c30c:0:0:c3]:5683/

Then you can discover and ping devices. A reset of the devices is maybe needed
to start a new session for the very first time.

This is the URL of the border-router, you can observe the temperature by connecting to this URL and using the `OBSERVE` method of CoAP

    coap://[aaaa::c30c:0:0:c3]:5683/temperature/push

To connect to the fan activator, use this url:
    
    coap://[aaaa::c30c:0:0:2eb]:5683/

You can change the threshold at which the mote will activate the activator by sending a POST request to this URL:

    coap://[aaaa::c30c:0:0:2eb]:5683/threshold

The payload must respect the following format:

    threshold=15

