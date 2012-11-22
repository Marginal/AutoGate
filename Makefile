include version.mak

PLUGIN=$(PROJECT)_$(VER).zip
PLUGIN_FILES=ReadMe.txt $(PROJECT)/lin.xpl $(PROJECT)/mac.xpl $(PROJECT)/win.xpl $(PROJECT)/64/lin.xpl $(PROJECT)/64/win.xpl

PACKAGE=$(PROJECT)_dev_$(VER).zip
PACKAGE_FILES=AutoGate.html Safedock\ Pilot\ Instructions.html imgs/*.png Jetways/*.blend Jetways/*.obj Jetways/*.dds Jetways/*.png DGSs/*.dds DGSs/*.obj Standalone_DGSs/*.dds Standalone_DGSs/*.obj

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
	./clone.py
	rm -f $(PACKAGE)
	zip -MM $(PACKAGE) $(PACKAGE_FILES)
