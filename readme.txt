me configure --set goahead.javascript=true --set goahead.cgi=true --set goahead.upload=true --set goahead.limitPost=5000000

add sqlite3 in /build/linux-x64-debug/platform.me
            libraries: [
                "rt",
                "dl", 
                "pthread",
                "m",
                "sqlite3",
            ],

me
rm build/linux-x64-debug/bin/libcrypto.a
rm build/linux-x64-debug/bin/libssl.a
me

sudo me install
sudo cp ./build/linux-x64-debug/bin/self.key  /etc/goahead/
sudo cp ./build/linux-x64-debug/bin/self.crt  /etc/goahead/

sudo goahead -v --home /etc/goahead ~/Perforce/webserver/www/
