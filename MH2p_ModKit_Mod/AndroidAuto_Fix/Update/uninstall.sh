#!/bin/ksh
# Copyright (c) 2025 fifthBro (https://github.com/fifthBro/pcm5-androidauto-connect-fix/)
# This Mod is part of MH2p SD ModKit, licensed under CC BY-NC-SA 4.0.
# https://creativecommons.org/licenses/by-nc-sa/4.0/
# See the LICENSE file in the project root for full license text.
# NOT FOR COMMERCIAL USE

# Uninstall Android Auto multi device fix
if [ "$OEM" = "PO" ]; then
        echo "Porsche detected, checking if Android Auto fix was applied"
        if [[ "$SOFTWARE_VERSION" == 26?? || "$SOFTWARE_VERSION" == 28?? ]]; then
                echo "Firmware $RELEASE_VERSION is within the affected range (26xx-28xx) -> Android Auto fix was required"
                [[ ! -e "/mnt/app" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
                mount -uw /mnt/app/
                if [[ -e "/mnt/app/eso/hmi/lsd/jars" ]]; then                    
                    if [[ -f "/mnt/app/eso/hmi/lsd/jars/aafix.jar" ]]; then
                        echo "Backing up: /mnt/app/eso/hmi/lsd/jars/aafix.jar"
                        cp -vf /mnt/app/eso/hmi/lsd/jars/aafix.jar $MOD_PATH/Backup/
                        echo "Removing: /mnt/app/eso/hmi/lsd/jars/aafix.jar"
                        rm -vf /mnt/app/eso/hmi/lsd/jars/aafix.jar
                    fi                                                  
                else
                        echo "error: /mnt/app/eso/hmi/lsd/jars does not exist"
                fi               
        else
                echo "Firmware $RELEASE_VERSION is outside the affected range (26xx-28xx) -> Android Auto fix was not required"                
        fi
fi