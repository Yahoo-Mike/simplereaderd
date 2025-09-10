# simplereaderd Server

SimpleReader is an EPUB and PDF reader written in Java and Kotlin for Android.

simplereaderd is a server for Ubuntu Server that synchronises a reader's
book, bookmarks and highlights across all devices.

## Ubuntu prereqs
To make this program, you must first install Drogon:
```
sudo apt update
sudo apt install cmake g++ uuid-dev zlib1g-dev libssl-dev git libjsoncpp-dev libsqlite3-dev pkg-config libsodium-dev -y
git clone https://github.com/drogonframework/drogon.git
cd drogon
git submodule update --init
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```
## configure Apache virtual host
Apache acts as proxy, handling TLS/SSL and passing unencrypted HTML to simplereaderd.  You can use your favourite proxy software.  You can also use any port, here 8443 is just an example.
```
<VirtualHost *:8443>
    ServerName yourdomain.com

    # TLS settings
    SSLEngine on
    SSLCertificateFile /etc/letsencrypt/live/yourdomain.com/fullchain.pem
    SSLCertificateKeyFile /etc/letsencrypt/live/yourdomain.com/privkey.pem

    # Proxy to Drogon on localhost:9000
    ProxyPreserveHost On
    ProxyPass        /upload http://127.0.0.1:9000/upload nocanon
    ProxyPassReverse /upload http://127.0.0.1:9000/upload
    ProxyPass        /        http://127.0.0.1:9000/
    ProxyPassReverse /        http://127.0.0.1:9000/
    LimitRequestBody 0
    ProxyTimeout 300
    RequestReadTimeout body=120

    # security headers
    Header always set X-Content-Type-Options "nosniff"
    Header always set X-Frame-Options "DENY"
</VirtualHost>
```
In /etc/apache2/ports.conf:
```
<IfModule ssl_module>
    Listen 443
    Listen 8443                 <== add this line
</IfModule>
```
Ensure Apache modules enabled, test and run:
```
sudo a2enmod ssl proxy proxy_http headers
sudo a2ensite your-site.conf
sudo apache2ctl configtest
sudo systemctl reload apache2
```
If you are using ufw, then allow this port:
```
sudo ufw allow 8443/tcp comment 'simplereaderd'
```
Note: depending on your server/WAN setup, you might need to forward port 8443 to your server (on your router).
## run simplereaderd as a daemon
Set up a user to run the daemon:
```
sudo adduser --system --no-create-home --group simplereaderd
sudo mkdir -p /var/lib/simplereader
sudo chown simplereaderd:simplereaderd /var/lib/simplereader
```
Install the binary and config:
```
sudo install -m 0755 build/simplereaderd /usr/local/bin/simplereaderd
sudo mkdir -p /etc/simplereader
sudo cp simplereader.conf /etc/simplereader/simplereader.conf
sudo chown -R root:root /etc/simplereader
sudo chown root:simplereaderd /etc/simplereader/simplereader.conf
sudo chmod 640 /etc/simplereader/simplereader.conf
```
Set up the daemon service in ```/etc/systemd/system/simplereaderd.service```:
```
[Unit]
Description=Simplereader daemon
After=network.target

[Service]
User=simplereaderd
Group=simplereaderd
ExecStart=/usr/local/bin/simplereaderd
WorkingDirectory=/var/lib/simplereader
Environment=SIMPLEREADER_CONF=/etc/simplereader/simplereader.conf
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
```
Enable the service and restart:
```
sudo systemctl daemon-reload
sudo systemctl enable simplereaderd
sudo systemctl start simplereaderd
sudo systemctl status simplereaderd
```
## tool:  add_user
To help you add a user into the database, the tool *add_user* is also packaged.  To build it:
```
gcc add_user.c -o add_user -lsqlite3 -lsodium
```
To run it: ```sudo add_user username password```
## License
SimpleReader is available under the Creative Commons license. See the [LICENSE](https://github.com/simplereaderd/License.md) file.