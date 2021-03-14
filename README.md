# Phoscon Daylight schedule updater

[Phoscon](https://phoscon.de/en/conbee2/) is the name of the fantastic Zigbee
home automation solution using hardware such as the deCONZ ConBee II. Along with
a standalone web-based configuration interface, there is even a REST API
provided with [documentation here](https://dresden-elektronik.github.io/deconz-rest-doc/)

## Background
While the deCONZ config pages do pretty much everything I want, one feature
I needed was to be able to program schedules using the inbuilt Daylight virtual
sensor to control my perimeter lights so they would come on at sunset, and
turn off at sunrise. This feature is not currently supported in the Phoscon
app, so I wrote this small daemon that fetches the sunrise and sunset times
based off a given location and then update the schedules in the deCONZ REST
API if the sun hours have changed.

## Prerequisites
- GLib/GIO
- libjansson
- libCURL
- Meson (Ninja)

## Building the binary
Clone the repository and after ensuring you have all the pre-reqs run:

```
meson build

ninja -C build
```

## Obtaining an API key
The deCONZ REST API requires each request contain a valid API key. This must
be obtained by first unlocking the deCONZ gateway, and then you can use curl
to POST a request using the **api_req_key.json** file in this repo:

`curl http://<gateway_host>:8088/api/ -d @api_key_req.json | jq `

(jq is optional and just used for pretty JSON formatting)

## Configuration file
Nearly the entirety of the configuration is contained within one file. There
is a commented sample file included in this repository which should be
updated with your values.

To find out the currently defined schedules, you can run this binary with the
the -l option which will list all the schedules and their IDs (which can
then be copied into the config file):
```
>./build/phoscon-sunmon -c sample.cfg -l
** Message: 15:44:57.761: Parsed 3 group(s) from config file 'sample.cfg'
** Message: 15:44:57.767: Schedule [2] 'MorningSun' Created: 2020-11-03  16:32:43 Status: enabled Time: W127/T05:22:32 (Local: W127/T06:22:32)
** Message: 15:44:57.767: Schedule [4] 'EveningSunset' Created: 2020-11-27  15:10:40 Status: enabled Time: W127/T17:09:31 (Local: W127/T18:09:31)
** Message: 15:44:57.768: Schedule [8] 'ShoeMoodOff' Created: 2021-01-02  17:15:40 Status: enabled Time: W127/T22:00:00 (Local: W127/T23:00:00)
** Message: 15:44:57.768: Schedule [9] 'ShoeMoodOn' Created: 2021-01-02  17:17:40 Status: enabled Time: W127/T17:09:31 (Local: W127/T18:09:31)
** Message: 15:44:57.768: Phoscon simple client initialised, found 4 schedules
** Message: 15:44:57.768: Phoscon schedule list (4 entries)
+-----+--------------------+------------+---------------------+-------------------+
| ID  | Name               | Status     | Created             | Schedule (local)  |
+-----+--------------------+------------+---------------------+-------------------+
| 008 | ShoeMoodOff        | enabled    | 2021-01-02 17:15:40 | W127/T23:00:00    |
| 002 | MorningSun         | enabled    | 2020-11-03 16:32:43 | W127/T06:22:32    |
| 009 | ShoeMoodOn         | enabled    | 2021-01-02 17:17:40 | W127/T18:09:31    |
| 004 | EveningSunset      | enabled    | 2020-11-27 15:10:40 | W127/T18:09:31    |
+-----+--------------------+------------+---------------------+-------------------+

```
In this case the sunset ID is 3 and the sunrise ID is 2.

### Why is this written in C and not <insert your choice of Go/Python/Rust/Java/Bash>?
Mainly because I like C. Even though it's arguably more code than say, a
Python program, there are many useful libraries available that make programming
in C quite manageable. Not to mention the added advantage of portability
and a small footprint.

# Attributions
The API for providing the sunrise and sunset times is
[Sunrise Sunset](https://sunrise-sunset.org/api)
Please show respect for a useful free-of-charge service by setting the poll
period to a sensible period (e.g once every 8 hours /  28800 seconds)

Also to Discord users @Mimiix and @Swoop in #deCONZ for the idea of using
the REST API instead of my original plan to write directly to the
SQLite database :)
