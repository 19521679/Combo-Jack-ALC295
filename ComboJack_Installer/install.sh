#!/bin/bash

if [[ $EUID -ne 0 ]];
then
    exec sudo /bin/bash "$0" "$@"
fi

cd "$( dirname "${BASH_SOURCE[0]}" )"

# Clean legacy stuff
#
sudo launchctl unload /Library/LaunchDaemons/khanhzum.ComboJack.plist 2>/dev/null
sudo rm -f /usr/local/bin/ALCPlugFix
sudo rm -f /Library/LaunchAgents/good.win.ALCPlugFix
sudo rm -f /Library/LaunchDaemons/good.win.ALCPlugFix
sudo rm -f /usr/local/bin/ComboJack/hda-verb
sudo rm -f /usr/local/share/ComboJack/Headphone.icns
sudo rm -f /usr/local/share/ComboJack/l10n.json

# install 

sudo mkdir -p /usr/local/share/ComboJack/
sudo cp Headphone.icns /usr/local/share/ComboJack/
sudo chmod 644 /usr/local/share/ComboJack/Headphone.icns
sudo cp l10n.json /usr/local/share/ComboJack/
sudo chmod 644 /usr/local/share/ComboJack/l10n.json

sudo cp khanhzumComboJack /usr/local/bin
sudo chmod 755 /usr/local/bin/khanhzumComboJack
sudo chown root:wheel /usr/local/bin/khanhzumComboJack
sudo cp khanhzum.ComboJack.plist /Library/LaunchAgents/
sudo chmod 644 /Library/LaunchAgents/khanhzum.ComboJack.plist
sudo chown root:wheel /Library/LaunchAgents/khanhzum.ComboJack.plist
sudo launchctl load /Library/LaunchAgents/khanhzum.ComboJack.plist
echo
echo "Please reboot! Also, it may be a good idea to turn off \"Use"
echo "ambient noise reduction\" when using an input method other than"
echo "the internal mic (meaning line-in, headset mic). As always: YMMV."
echo
echo "You can check to see if the watcher is working in the IORegistry:"
echo "there should be a device named \"VerbStubUserClient\" attached to"
echo "\"com_XPS_SetVerb\" somewhere within the \"HDEF\" entry's hierarchy."
echo
echo "Enjoy!"
echo
exit 0
