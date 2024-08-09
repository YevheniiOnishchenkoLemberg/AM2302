#!/bin/ash

get_data_from_driver()
{
    export RESULT=$(cat /dev/am2302)
}

find_display_driver_bin()
{
    export DISPLAY_DRIVER=$(find / -name ssd1306_bin | head -n1)
    if [ -z $DISPLAY_DRIVER ]; then
        echo "ERROR: couldn't find display driver binary file!"
    fi
}

send_data_to_display()
{
    get_data_from_driver
    
    # init and clear display
    $DISPLAY_DRIVER -I 128x64
    $DISPLAY_DRIVER -d 1
    $DISPLAY_DRIVER -c

    # get rid of driver name
    RESULT=$(echo $RESULT | sed 's/ /\\n/g' | sed 's/\[AM2302\]://g')
    $DISPLAY_DRIVER -m $RESULT
}

if [ -z $DISPLAY_DRIVER ]; then
    find_display_driver_bin
fi
send_data_to_display