include version.mak

PLUGIN=$(PROJECT).zip
PLUGIN_FILES=ReadMe.txt $(PROJECT)/lin.xpl $(PROJECT)/mac.xpl $(PROJECT)/win.xpl $(PROJECT)/64/lin.xpl $(PROJECT)/64/win.xpl

PACKAGE=$(PROJECT)_dev_$(VER).zip
PACKAGE_FILES=AutoGate.html Safedock\ Pilot\ Instructions.html imgs/*.png objs/AutoGate*.blend objs/*.obj objs/*.dds objs/*.png

INSTALLDIR=~/Desktop/X-Plane\ 10/Resources/plugins

all:	$(PLUGIN) $(PACKAGE)

clean:
	rm $(PLUGIN) $(PACKAGE)

install:	$(PLUGIN)
	rm -rf $(INSTALLDIR)/$(PROJECT)
	unzip -o -d $(INSTALLDIR) $(PLUGIN)

$(PLUGIN):	$(PLUGIN_FILES)
	chmod +x $(PROJECT)/*.xpl $(PROJECT)/64/*.xpl
	rm -f $(PLUGIN)
	zip -MM $(PLUGIN) $(PLUGIN_FILES)

$(PACKAGE):	$(PACKAGE_FILES)
	rm -f $(PACKAGE)
	zip -MM $(PACKAGE) $(PACKAGE_FILES)
