Clamz is a little command-line program to download MP3 files from Amazon.com's music store.  It is intended to serve as a substitute for Amazon's official MP3 Downloader, which is not free software (and therefore is only available in binary form for a limited set of platforms.)  Clamz can be used to download either individual songs or complete albums that you have purchased from Amazon.

Please keep in mind that there is _absolutely no warranty_ of any kind.  **Use this software at your own risk.**

## Warning (2012-09-17) ##
**Currently, Amazon's US store is serving garbage AMZ files to users with a Linux User-Agent.**  Furthermore, the Amazon Cloud Player is blocking Linux users from downloading multiple files at a time.  See discussion (and possible workarounds) on bugs [#35](https://code.google.com/p/clamz/issues/detail?id=35) and [#36](https://code.google.com/p/clamz/issues/detail?id=36).

## Installation ##

A few OS distributions now include clamz packages/ports, so you may want to consult your friendly neighborhood package manager first.

Clamz uses the libraries [libgcrypt](http://www.gnupg.org/), [libcurl](http://curl.haxx.se/), and [libexpat](http://expat.sourceforge.net/), so you will need to have all three libraries installed (including "development" packages, if any) before compiling clamz.

After downloading the source package, run the following commands to unpack and compile clamz:
```
 tar xfvz clamz-0.5.tar.gz

 cd clamz-0.5

 ./configure

 make
```
To install it, run (as root):
```
 make install
```

**After installing clamz, click one of the following links to enable the "MP3 downloader" mode in Amazon's web store: [(US)](http://www.amazon.com/gp/dmusic/after_download_manager_install.html?AMDVersion=1.0.9)  [(UK)](http://www.amazon.co.uk/gp/dmusic/after_download_manager_install.html?AMDVersion=1.0.9)  [(France)](http://www.amazon.fr/gp/dmusic/after_download_manager_install.html?AMDVersion=1.0.9)  [(Germany)](http://www.amazon.de/gp/dmusic/after_download_manager_install.html?AMDVersion=1.0.9)  [(Japan)](http://www.amazon.co.jp/gp/dmusic/after_download_manager_install.html?AMDVersion=1.0.9)**

You can then proceed to purchase MP3 songs or albums.  When you do so, you will be given a `.amz` file (a small, encrypted data file); run clamz on this file to download the actual MP3 music files.  See the clamz manpage (`man clamz`) and README file for more information.