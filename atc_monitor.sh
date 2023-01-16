#!/bin/bash

# This software will report on the MQTT messages sent by the ESP32
# software.  It'll report the time/temp/humidity values and when each
# ESP32 sent a message (based on what is on each topic).  This works
# because the ESP32 sets the "retain" value on the message, so reading
# the topic will report on the last message received
#
# Set the server at the top, and at the bottom set the do_line values
# to match the name's sent from the esp32
#
# We get a pretty picture looking something like
#
#                |         Date        |   Age  | TempF | Humid | Batt%
#                | =================== | ====== | ===== | ===== | =====
#       Bathroom | 2023/01/15 21:09:38 |     33 |  66.6 |    45 |    46
#        Bedroom | 2023/01/15 21:08:48 |     83 |  70.1 |    40 |    83
#     Guest Room | 2023/01/15 21:09:56 |     15 |  73.7 |    36 |    62
#        Kitchen | 2023/01/15 21:09:49 |     22 |  70.4 |    39 |    91
#    Living Room | 2023/01/15 21:09:24 |     47 |  71.4 |    38 |    52
#
#
#  ESP32 10.100.200.56: 2023/01/15 21:09:38 (33 seconds ago)   
#  ESP32 10.100.200.57: 2023/01/15 21:09:49 (22 seconds ago) 

SERVER=hass

headfmt="%-15s | %19s | %6s | %5s | %5s | %5s\n"
    fmt="%15s | %19s | %6s | %5.1f | %5s | %5s\n"

do_line()
{
  head=$1
  field=$2
  rx=${rx[$field]}
  if [ -z "$rx" ]
  then
    age=-
  else
    age=$((now-$rx))
  fi
  printf "$fmt" "$head" "${date[$field]}" "$age" "${tempF[$field]}" "${humid[$field]}" "${batt[$field]}" | tr -d '"'
}

typeset -A rx date tempF humid batt ip

# atc/# are the Xiaomi Mijia thermometers
#    https://github.com/sweharris/esp32_atc_to_mqtt )
#    CT50 thermometer and publishes the output of http://thermostat/tstat
coproc mosquitto_sub --host $SERVER -t 'atc/#' -v

datefmt='%Y/%m/%d %H:%M:%S'

clear

while [ 1 ]
do
  read -t 5 line

  now=$(date +%s)

  if [ -n "$line" ]
  then
    t=${line#*\"time\":} ; t=${t%%,*}
    name=${line#*\"name\":\"} ; name=${name%%\"*}
    tempF=${line#*\"tempF\":} ; tempF=${tempF%%,*}
    humid=${line#*\"humidity\":} ; humid=${humid%%,*}
    batt=${line#*\"battpct\":} ; batt=${batt%?}
    rxby=${line#*\"rx_by\":\"} ; rxby=${rxby%%\"*}
    ip["$rxby"]=$t

    date=$(date +"$datefmt" -d @$t)

    rx[$name]=$t
    date[$name]=$date
    tempF[$name]=$tempF
    humid[$name]=$humid
    batt[$name]=$batt
  fi

  tput cup 0 0

  printf "$headfmt" "" "Date       " "Age " "TempF" "Humid" "Batt%"
  printf "$headfmt" "" "===================" "======" "=====" "=====" "====="

  # Example of sensors to display
  do_line "Bathroom" ATC_BATHRM
  do_line "Bedroom" ATC_BEDRM
  do_line "Guest Room" ATC_GUEST
  do_line "Kitchen" ATC_KITCHN
  do_line "Living Room" ATC_LVROOM

  echo
  echo
  for a in "${!ip[@]}"
  do
    t=${ip[$a]} 
    let d=$now-$t
    echo "  ESP32 $a: $(date -d @$t +"$datefmt") ($d seconds ago)   "
  done | sort
done <&"${COPROC[0]}"
