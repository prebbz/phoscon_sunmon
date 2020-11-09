# Phoscon Daylight schedule updater

[Phoscon](https://phoscon.de/en/conbee2/) is the name of the fantastic Zigbee home automation solution using hardware such as the deCONZ ConBee II. Along with a standalone configuration interface, there is even a REST API provided with [documentation here](https://dresden-elektronik.github.io/deconz-rest-doc/)

## Background
While the deCONZ config pages do pretty much everything I want, one feature I needed was to be able to program a schedule to use the inbuilt Daylight virtual sensor (so my perimeter lights would come on at sunset, and go off at sunrise). This feature is not currently supported in the Phoscon app, so I wrote a small daemon that would fetch the sunrise and sunset times based from a given location and then update the REST API if they are updated.

## Prerequisites
- GLib/GIO
- libjansson
- libCURL
- MESON (Ninja)

## Building the binary
Clone the repository and after ensuring you have all the pre-reqs run:

```
meson build

ninja -C build
```

## Obtaining an API key
The deCONZ REST API requires each request contain a valid API key. This must be obtained by first unlocking the gateway, and then you can use curl to POST a request using the **api_req_key.json** file in this repo:

`curl http://<gateway_host>:8088/api/ -d @../api_key_req.json | jq `

## Configuration file
Nearly the entirety of the configuration is contained within one file. There is a commented sample file included in this repository,  adjust it with your values.

My suggestion is to create the schedules you want for Sunrise and Sunset first using the Phoscon web client and then run this binary with an empty [schedules} section. It will then print out all schedules along with the corresponding ID which can then be used in the configuration file:
```
>./build/phoscon-sunmon -c sample.cfg
** Message: 22:56:56.762: Parsed 3 key(s) from group 'phoscon'
** Message: 22:56:56.762: Parsed 3 key(s) from group 'general'
** Message: 22:56:56.762: Parsed 2 key(s) from group 'schedules'
** Message: 22:56:56.762: Parsed 3 group(s) from config file 'sample.cfg'
** Message: 22:56:57.405: Sunrise/Sunset client initialised with location lat=55.1035667 long=17.933340
** Message: 22:56:57.405: Attribution of API to sunrise-sunset.org
** Message: 22:56:57.406: Initial sunrise time (UTC): 06:31:22
** Message: 22:56:57.406: Initial sunset time (UTC) : 15:10:30
** Message: 22:56:57.409: Schedule [2] 'MorningSun' Created: 2020-11-03  16:32:43 Status: enabled Time: W127/T06:31:22 (Local: W127/T07:31:22)
** Message: 22:56:57.409: Schedule [3] 'EveningSunset' Created: 2020-11-03  16:34:07 Status: enabled Time: W127/T15:10:30 (Local: W127/T16:10:30)
** Message: 22:56:57.409: Phoscon simple client initialised, found 2 schedules
** Message: 22:56:57.409: Sunrise/sunset poll period is 3600 seconds
```
In this case the sunset ID is 3 and the sunrise ID is 2.

### Why is written in C and not <insert your choice of Go/Python/Rust/Java/Bash>?
Mainly because I like C.  Even though it's arguably more code than say, a Python file, there are many useful libraries available that make it programming in C quite manageable.  Then there is the added advantage of portability and light-weight.

# Attributions
The API for providing the sunrise and sunset times is [Sunrise Sunset](https://sunrise-sunset.org/api)
Please show respect for a useful free-of-charge service by setting the poll period to a sensible period (e.g once every 8 hours /  28800 seconds)

Also to Discord users @Mimiix and @Swoop in #deCONZ for the idea of using the REST API.
