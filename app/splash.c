#include "app.h"

#define kSplashImageWidth 128
#define kSplashImageHeight 100
#define kSplashImageRowBytes (kSplashImageWidth / 8)
#define kSplashImage4BitRowBytes (kSplashImageWidth / 2)

/*
    CopyBits below reads this bitmap's rows in word/longword-sized
    chunks internally. A plain `unsigned char[]` only guarantees
    1-byte alignment by C's own rules, so the linker is free to place
    it at an odd address -- harmless on a 68020+ (and apparently on
    Mini vMac's CPU emulation, which never caught this), but a real
    68000 raises an Address Error immediately on any misaligned
    word/long access. Confirmed live: this exact crash, at startup,
    on a real Mac Plus, on both disk images -- not a guess.
*/

static const unsigned char kSplashImageBits[kSplashImageHeight * kSplashImageRowBytes] __attribute__((aligned(4))) = {
#include "splash_image.h"          /* 1-bit fallback */
};

static const unsigned char
	kSplashImage4BitBits[
		kSplashImageHeight * kSplashImage4BitRowBytes
	] __attribute__((aligned(4))) = {
	#include "splash_image_4bit.h"
};

static const ColorSpec kSplashImage4BitPalette[16] = {
	#include "splash_palette_4bit.h"
};

/* Bump this on every release. */
static const unsigned char kVersionString[] = "\pv0.1.2-alpha";
static const unsigned char kGitHubURL[] = "\pgithub.com/ActionRetro";

typedef struct {
    long ctSeed;
    short ctFlags;
    short ctSize;
    ColorSpec ctTable[16];
} SplashColorTable;

static Boolean SplashScreenSupports4Bit(void)
{
    long response;
    GDHandle device;
    PixMapHandle pixMap;

    if (Gestalt(gestaltQuickdrawVersion, &response) != noErr)
        return false;

    if (response < gestalt8BitQD)
        return false;

    device = GetMainDevice();

    if (device == NULL)
        return false;

    pixMap = (**device).gdPMap;

    if (pixMap == NULL)
        return false;

    return ((**pixMap).pixelSize >= 4);
}

static void Draw4BitSplash(DialogPtr dlg, const Rect *destRect)
{
    PixMap image;
    CTabHandle tableHandle;
    SplashColorTable *table;
    short i;

    tableHandle =
        (CTabHandle) NewHandle(sizeof(SplashColorTable));

    if (tableHandle == NULL)
        return;

    HLock((Handle) tableHandle);

    table = (SplashColorTable *) *tableHandle;
    table->ctSeed = GetCTSeed();
    table->ctFlags = 0;
    table->ctSize = 15; /* Number of entries minus one. */

    for (i = 0; i < 16; i++)
        table->ctTable[i] = kSplashImage4BitPalette[i];

    image.baseAddr = (Ptr) kSplashImage4BitBits;

    /*
        Bit 15 tells Color QuickDraw this is a PixMap rather than
        an old monochrome BitMap.
    */
    image.rowBytes =
        (short) (0x8000 | kSplashImage4BitRowBytes);

    SetRect(
        &image.bounds,
        0,
        0,
        kSplashImageWidth,
        kSplashImageHeight
    );

    image.pmVersion = 0;
    image.packType = 0;
    image.packSize = 0;
    image.hRes = 72L << 16;
    image.vRes = 72L << 16;
    image.pixelType = 0;  /* Indexed pixels. */
    image.pixelSize = 4;
    image.cmpCount = 1;
    image.cmpSize = 4;
    image.planeBytes = 0;
    image.pmTable = tableHandle;
    image.pmReserved = 0;

    CopyBits(
        (BitMap *) &image,
        &((GrafPtr) dlg)->portBits,
        &image.bounds,
        destRect,
        srcCopy,
        NULL
    );

    HUnlock((Handle) tableHandle);
    DisposeHandle((Handle) tableHandle);
}

static void Draw1BitSplash(DialogPtr dlg, const Rect *destRect)
{
    BitMap image;

    image.baseAddr = (Ptr) kSplashImageBits;
    image.rowBytes = kSplashImageRowBytes;

    SetRect(
        &image.bounds,
        0,
        0,
        kSplashImageWidth,
        kSplashImageHeight
    );

    CopyBits(
        &image,
        &((GrafPtr) dlg)->portBits,
        &image.bounds,
        destRect,
        srcCopy,
        NULL
    );
}

static pascal void DrawSplashTitle(DialogPtr dlg, short itemNo)
{
    DialogItemType type;
    Handle itemH;
    Rect box;
    short textWidth;
    short genevaFont;
    Str255 s;
    /* BitMap image; */
    Rect imageRect;

    GetDialogItem(dlg, itemNo, &type, &itemH, &box);
    SetPort(dlg);

    TextFont(0);
    TextSize(12);
    TextFace(bold);
    BlockMove("\pThe Artful Type", s, 16);
    textWidth = StringWidth(s);
    MoveTo(box.left + (box.right - box.left - textWidth) / 2, box.top + 18);
    DrawString(s);
	
	/*
    image.baseAddr = (Ptr)kSplashImageBits;
    image.rowBytes = kSplashImageRowBytes;
    SetRect(&image.bounds, 0, 0, kSplashImageWidth, kSplashImageHeight);
    SetRect(&imageRect, 0, 0, kSplashImageWidth, kSplashImageHeight);
    OffsetRect(&imageRect,
        box.left + (box.right - box.left - kSplashImageWidth) / 2,
        box.top + 28);
    CopyBits(&image, &((GrafPtr)dlg)->portBits, &image.bounds, &imageRect, srcCopy, NULL);
	*/
	
	SetRect(
		&imageRect,
		0,
		0,
		kSplashImageWidth,
		kSplashImageHeight
	);

	OffsetRect(
		&imageRect,
		box.left +
			(box.right - box.left - kSplashImageWidth) / 2,
		box.top + 28
	);

	if (SplashScreenSupports4Bit())
		Draw4BitSplash(dlg, &imageRect);
	else
		Draw1BitSplash(dlg, &imageRect);

	GetFNum("\pGeneva", &genevaFont);
	
	TextFont(genevaFont);
    TextFace(normal);
    TextSize(10);
    BlockMove("\pA Distraction-Free Writing Environment", s, 39);
    textWidth = StringWidth(s);
    MoveTo(box.left + (box.right - box.left - textWidth) / 2, box.top + 144);
    DrawString(s);

    BlockMove(kVersionString, s, kVersionString[0] + 1);
    textWidth = StringWidth(s);
    MoveTo(box.left + (box.right - box.left - textWidth) / 2, box.top + 158);
    DrawString(s);

    TextFace(bold);
    BlockMove(kGitHubURL, s, kGitHubURL[0] + 1);
    textWidth = StringWidth(s);
    MoveTo(box.left + (box.right - box.left - textWidth) / 2, box.top + 172);
    DrawString(s);
    TextFace(normal);
}

/*
    The DLOG resource's bounds only center it on a 512x342 compact Mac
    screen. Reposition here instead, based on the actual screen size,
    so it's centered on any resolution. The DLOG is marked invisible
    so this happens before the dialog is ever shown -- no flash at
    the wrong position.
*/
static void CenterAndShowDialog(DialogPtr dlg)
{
    Rect screenBounds;
    Rect dlgBounds;
    short dlgWidth, dlgHeight;
    short newLeft, newTop;

    screenBounds = qd.screenBits.bounds;
    screenBounds.top += MENU_BAR_HEIGHT;
    dlgBounds = ((GrafPtr) dlg)->portRect;
    dlgWidth = dlgBounds.right - dlgBounds.left;
    dlgHeight = dlgBounds.bottom - dlgBounds.top;
    newLeft = screenBounds.left + (screenBounds.right - screenBounds.left - dlgWidth) / 2;
    newTop = screenBounds.top + (screenBounds.bottom - screenBounds.top - dlgHeight) / 2;
    MoveWindow(dlg, newLeft, newTop, false);
    ShowWindow(dlg);
}

void ShowSplashScreen(void)
{
    DialogPtr dlg;
    short item;
    DialogItemType type;
    Handle itemH;
    Rect box;
    Boolean done = false;

    while (!done) {
        dlg = GetNewDialog(kSplashDialog, NULL, (WindowPtr) -1L);
        if (dlg == NULL)
            return;

        CenterAndShowDialog(dlg);

        GetDialogItem(dlg, iSplashTitle, &type, &itemH, &box);
        SetDialogItem(dlg, iSplashTitle, type, (Handle) NewUserItemUPP(DrawSplashTitle), &box);

        do {
            ModalDialog(NULL, &item);
        } while (item != iSplashNew && item != iSplashOpen);

        DisposeDialog(dlg);
        SetPort(gWindow);
        UpdateMenuBarLook();

        /* Open Document, then Cancel in the file picker -- show the splash again */
        done = (item == iSplashNew) || DoOpenFile();
    }
}

/* Help -> About: same branding dialog as the startup splash, but with
   a single OK button instead of New/Open, since this is shown during
   active editing rather than at startup. */
void ShowAboutBox(void)
{
    DialogPtr dlg;
    short item;
    DialogItemType type;
    Handle itemH;
    Rect box;

    dlg = GetNewDialog(kAboutDialog, NULL, (WindowPtr) -1L);
    if (dlg == NULL)
        return;

    CenterAndShowDialog(dlg);

    GetDialogItem(dlg, iAboutTitle, &type, &itemH, &box);
    SetDialogItem(dlg, iAboutTitle, type, (Handle) NewUserItemUPP(DrawSplashTitle), &box);

    do {
        ModalDialog(NULL, &item);
    } while (item != iAboutOK);

    DisposeDialog(dlg);
    SetPort(gWindow);
    UpdateMenuBarLook();
}
