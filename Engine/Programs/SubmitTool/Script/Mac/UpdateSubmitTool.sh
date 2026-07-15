#!/bin/sh

zip=$1 shift
folder=$1 shift
name=$1 shift
version=$1 shift
versionfile=$1 shift
executable=$1 shift
rootdir=$1 shift

executableargs=""
for var in "$@"
do
    executableargs="$executableargs $var"
done

sleep 2s

killall $name

# Not removing the folder to be safe and maintain parity with windows, re-enable if we find a reason
#echo "Deleting $folder"
#rm -rvf $folder

mkdir -p "$folder"

unzip -o "$zip" -d "$folder"

rm -f "$versionfile"
versiondir=${versionfile%/*}
mkdir -p "$versiondir"
printf '%s' "$version" > $versionfile

nohup "$executable" $executableargs -root-dir "$rootdir" > /dev/null 2>&1 &