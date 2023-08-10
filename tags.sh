echo -e "Enter Debian package version"
read tag

echo -e "Enter platform, rbp or vero"
read platform

git tag -a kodi-$platform-$tag-osmc -m "OSMC Kodi release"
echo -e "Tag created successfully"
git push origin kodi-$platform-$tag-osmc
