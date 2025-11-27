#!/bin/ksh
# Copyright (c) 2025 fifthBro (https://github.com/fifthBro/pcm5-androidauto-connect-fix/)
# This Mod is part of MH2p SD ModKit, licensed under CC BY-NC-SA 4.0.
# https://creativecommons.org/licenses/by-nc-sa/4.0/
# See the LICENSE file in the project root for full license text.
# NOT FOR COMMERCIAL USE

# backup

[[ ! -e $MOD_PATH/Backup ]] && mkdir $MOD_PATH/Backup

# Fix the annoying Android Auto multi device bug
if [ "$OEM" = "PO" ]; then
        echo "Porsche detected, checking if Android Auto fix is needed"
        if [[ "$SOFTWARE_VERSION" == 26?? || "$SOFTWARE_VERSION" == 28?? ]]; then
                echo "Firmware $RELEASE_VERSION is within the affected range (26xx-28xx) -> Android Auto fix is required"
                [[ ! -e "/mnt/app" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
                mount -uw /mnt/app/
                if [[ -e "/mnt/app/eso/hmi/lsd/jars" ]]; then
                        if [[ -f "$MOD_PATH/aafix.jar" ]]; then
                            if [[ -f "/mnt/app/eso/hmi/lsd/jars/aafix.jar" ]]; then
                                echo "warning: /mnt/app/eso/hmi/lsd/jars/aafix.jar already exists"
                                cp -vf /mnt/app/eso/hmi/lsd/jars/aafix.jar $MOD_PATH/Backup/
                            fi
                            cp -vf $MOD_PATH/aafix.jar /mnt/app/eso/hmi/lsd/jars/aafix.jar
                        else
                            echo "error: cannot find $MOD_PATH/aafix.jar"
                        fi
                else
                        echo "error: /mnt/app/eso/hmi/lsd/jars does not exist"
                fi               
                if [[ -e "/mnt/persist_new/persistence/persistence.sqlite" ]]; then
                    if [[ -e "$MOD_PATH/fix_partition_1008" ]]; then
                        if [[ -e "/armle/usr/lib/libsqlite3.so" && -e "/usr/lib/libz.so" ]]; then
                            cp -vf /mnt/persist_new/persistence/persistence.sqlite $MOD_PATH/Backup/persistence.sqlite_orig
                            LD_LIBRARY_PATH="${LD_LIBRARY_PATH:+$LD_LIBRARY_PATH:}/armle/usr/lib:/usr/lib" $MOD_PATH/fix_partition_1008 --fix
                            cp -vf /mnt/persist_new/persistence/persistence.sqlite $MOD_PATH/Backup/persistence.sqlite_patched
                        else
                            echo "error: cannot find system libraries"
                        fi
                    else
                        echo "error: cannot find $MOD_PATH/fix_partition_1008 tool"
                    fi
                else
                    echo "error: cannot find /mnt/persist_new/persistence/persistence.sqlite"
                fi        
        else
                echo "Firmware $RELEASE_VERSION is outside the affected range (26xx-28xx) -> Android Auto fix is not required"                
        fi
fi
