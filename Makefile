include version.mak

PLUGIN=$(PROJECT)_$(VER).zip
PLUGIN_FILES=ReadMe.txt Pilot\ Instructions,\ Type\ 2S\ display.pdf $(PROJECT)/alert.wav $(PROJECT)/lin.xpl $(PROJECT)/mac.xpl $(PROJECT)/win.xpl $(PROJECT)/OpenAL32.dll $(PROJECT)/32/win.xpl $(PROJECT)/64/lin.xpl $(PROJECT)/64/win.xpl

PACKAGE=$(PROJECT)_kit_$(VER).zip
PACKAGE_FILES=AutoGate.html imgs/*.jpeg Jetways/*.dds Jetways/*.png Jetways/*.obj DGSs/*.dds DGSs/*.obj Standalone_DGSs/*.dds Standalone_DGSs/*.obj

INSTALLDIR=~/Desktop/X-Plane\ 10/Resources/plugins

all:	$(PLUGIN) $(PACKAGE)

clean:
	rm $(PLUGIN) $(PACKAGE)

install:	$(PLUGIN)
	rm -rf $(INSTALLDIR)/$(PROJECT)
	unzip -o -d $(INSTALLDIR) $(PLUGIN)

$(PLUGIN):	$(PLUGIN_FILES)
	touch $(PLUGIN_FILES)
	chmod +x $(PROJECT)/*.xpl $(PROJECT)/32/*.xpl $(PROJECT)/64/*.xpl
	rm -f $(PLUGIN)
	zip -MM -o $(PLUGIN) $(PLUGIN_FILES)

$(PACKAGE):	$(PACKAGE_FILES)
	./clone.py
	rm -f $(PACKAGE)
	zip -MM $(PACKAGE) $(PACKAGE_FILES)
