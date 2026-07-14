/*
    Milestone 2: a real distraction-free Markdown editor.
    Full-screen window, wide margins, 14pt Times, File menu with
    Save/Open backed by the classic File Manager. Saving straight to
    the BlueSCSI SD card (bypassing this disk's HFS volume) is a
    later milestone -- this still saves onto the boot disk itself.
*/

#include "app.h"

WindowPtr gWindow;
TEHandle gTE;
TEHandle gHiddenTE;
TEHandle gActiveTE;
ControlHandle gScrollBar;
Boolean gScrollBarVisible = false;
Boolean gDone = false;
Boolean gHaveFile = false;
Boolean gDirty = false;
Str255 gFileName;
short gVRefNum;
MenuHandle gViewMenu;
MenuHandle gEditMenu;
MenuHandle gFontMenu;
Boolean gHideMarkdown = true;
short gZoomIndex = kZoomBaselineIndex;

UndoSnapshot gUndoStack[MAX_UNDO_LEVELS];
short gUndoCount = 0;
UndoSnapshot gRedoStack[MAX_UNDO_LEVELS];
short gRedoCount = 0;
Boolean gTypingRunActive = false;

Str255 gLinkURLs[MAX_LINKS + 1];
short gLinkCount = 0;

short gBodyFontNum;
Str255 gBodyFontName = "\pGeneva";

short gMarginSize = MARGIN_SMALL;

static void Init(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
}

/*
    Writer mode gets a black menu bar with white text; Markdown mode gets
    the standard look. There's no Menu Manager API for this on classic Mac
    OS (that's a much later Appearance Manager concept) -- on a 1-bit
    display, drawing the normal bar and then XOR-inverting that strip
    achieves the same thing trivially. Must target the Window Manager
    port (global screen coordinates), not whatever window's port happens
    to be current, since the menu bar isn't part of any window.
*/
void UpdateMenuBarLook(void)
{
    GrafPtr savePort;
    GrafPtr wMgrPort;
    Rect bar;

    DrawMenuBar();

    if (gHideMarkdown) {
        GetPort(&savePort);
        GetWMgrPort(&wMgrPort);
        SetPort(wMgrPort);

        SetRect(&bar, 0, 0, qd.screenBits.bounds.right, MENU_BAR_HEIGHT);
        InvertRect(&bar);

        SetPort(savePort);
    }
}


static void MakeMenu(void)
{
    MenuHandle fileMenu;
    MenuHandle styleMenu;
    MenuHandle helpMenu;

    fileMenu = NewMenu(mFile, "\pFile");
    AppendMenu(fileMenu, "\pNew/N;Open.../O;Save/S;Save As...;(-;Quit/Q");
    InsertMenu(fileMenu, 0);

    /* No "/" shortcut on Redo -- it would register as a second cmd-key
       equivalent for the same letter as Undo, ambiguous to MenuKey.
       Cmd-Shift-Z for Redo is instead handled directly in EventLoop,
       intercepted before MenuKey ever sees it. */
    gEditMenu = NewMenu(mEdit, "\pEdit");
    AppendMenu(gEditMenu, "\pUndo/Z;Redo;(-;Cut/X;Copy/C;Paste/V;(-;Select All/A");
    InsertMenu(gEditMenu, 0);
    DisableItem(gEditMenu, iUndo);
    DisableItem(gEditMenu, iRedo);

    styleMenu = NewMenu(mStyle, "\pStyle");
    AppendMenu(styleMenu, "\pBold/B;Italic/I;Code/K;Strikethrough;(-;Body;Heading 1/1;Heading 2/2;Heading 3/3;(-;Link/L;(-;None");
    InsertMenu(styleMenu, 0);
		
	gFontMenu = NewMenu(mFont, "\pFont");
	AppendMenu(gFontMenu, "\pChicago;Geneva;Helvetica;New York;Palatino;Times");
	InsertMenu(gFontMenu, 0);
	CheckItem(gFontMenu, iFontGeneva, true);
	
    gViewMenu = NewMenu(mView, "\pView");
    AppendMenu(gViewMenu,"\pMarkdown;Writer;(-;10 pt;12 pt;14 pt;16 pt;18 pt;20 pt;(-;Small Margins;Medium Margins;Large Margins");
    InsertMenu(gViewMenu, 0);
    CheckItem(gViewMenu, iWriterView, true);
    CheckItem(gViewMenu, iMarginSmall, true);

    helpMenu = NewMenu(mHelp, "\pHelp");
    AppendMenu(helpMenu, "\pAbout CloudType...");
    InsertMenu(helpMenu, 0);

    UpdateMenuBarLook();
}

static void UpdateFontSizeChecks(void)
{
    CheckItem(gViewMenu, iFont10, gZoomIndex == 0);
    CheckItem(gViewMenu, iFont12, gZoomIndex == 1);
    CheckItem(gViewMenu, iFont14, gZoomIndex == 2);
    CheckItem(gViewMenu, iFont16, gZoomIndex == 3);
    CheckItem(gViewMenu, iFont18, gZoomIndex == 4);
    CheckItem(gViewMenu, iFont20, gZoomIndex == 5);
}

static void UpdateMarginMenuChecks(short selectedItem)
{
    CheckItem(
        gViewMenu,
        iMarginSmall,
        selectedItem == iMarginSmall
    );

    CheckItem(
        gViewMenu,
        iMarginMedium,
        selectedItem == iMarginMedium
    );

    CheckItem(
        gViewMenu,
        iMarginLarge,
        selectedItem == iMarginLarge
    );
}

static void ApplyMarginSize(short marginSize)
{
    Rect viewRect;
    Rect sbRect;
    short oldTop;

    if (gWindow == NULL)
        return;

    gMarginSize = marginSize;

    SetPort(gWindow);

    /*
        Preserve the current vertical scroll position while changing
        only the horizontal layout.
    */
    oldTop = (**gActiveTE).destRect.top;

    viewRect = gWindow->portRect;
    viewRect.left += gMarginSize;
    viewRect.right -= gMarginSize;
    viewRect.top += MARGIN_TOP;
    viewRect.bottom -= MARGIN_BOTTOM;

    /*
        Update both the Markdown and Writer TextEdit records.
    */
    (**gTE).viewRect = viewRect;
    (**gTE).destRect.left = viewRect.left;
    (**gTE).destRect.right = viewRect.right;

    (**gHiddenTE).viewRect = viewRect;
    (**gHiddenTE).destRect.left = viewRect.left;
    (**gHiddenTE).destRect.right = viewRect.right;

    /*
        Keep the existing vertical scroll position.
    */
    (**gTE).destRect.top = oldTop;
    (**gHiddenTE).destRect.top = oldTop;

    /*
        Reposition and resize the scrollbar.
    */
    sbRect = viewRect;
    sbRect.left =
        viewRect.right + (gMarginSize - SCROLLBAR_WIDTH) / 2;
    sbRect.right = sbRect.left + SCROLLBAR_WIDTH;
    sbRect.top -= 1;
    sbRect.bottom += 1;

    MoveControl(gScrollBar, sbRect.left, sbRect.top);
    SizeControl(
        gScrollBar,
        sbRect.right - sbRect.left,
        sbRect.bottom - sbRect.top
    );

    TECalText(gTE);
    TECalText(gHiddenTE);
    AdjustScrollbar();

    InvalRect(&gWindow->portRect);
}

short CurrentBodyFont(void)
{
    return gBodyFontNum;
}
static void UpdateFontMenuChecks(short selectedItem)
{
    short item;

    for (item = iFontChicago; item <= iFontTimes; item++)
        CheckItem(gFontMenu, item, item == selectedItem);
}
static void SetBodyFontByName(ConstStr255Param fontName, short menuItem)
{
    short fontNum;

    GetFNum(fontName, &fontNum);

    /*
        Font number zero can represent the system font, including
        Chicago, so do not treat zero automatically as failure.
    */
    gBodyFontNum = fontNum;

    BlockMove(
        (Ptr) fontName,
        (Ptr) gBodyFontName,
        fontName[0] + 1
    );

    UpdateFontMenuChecks(menuItem);
    RebuildTextUsingBodyFont();
}

static void MakeWindow(void)
{
    Rect bounds;
    Rect viewRect;
    Rect sbRect;
    short fontNum;

    bounds = qd.screenBits.bounds;
    bounds.top += MENU_BAR_HEIGHT;

    gWindow = NewWindow(NULL, &bounds, "\p", true, plainDBox,
                         (WindowPtr) -1L, false, 0);
    SetPort(gWindow);

    GetFNum(gBodyFontName, &gBodyFontNum);
    fontNum = gBodyFontNum;
    TextFont(fontNum);
    TextSize(CurrentFontSize());

    viewRect = gWindow->portRect;
    viewRect.left += gMarginSize;
    viewRect.right -= gMarginSize;
    viewRect.top += MARGIN_TOP;
    viewRect.bottom -= MARGIN_BOTTOM;

    gTE = TEStyleNew(&viewRect, &viewRect);
    gHiddenTE = TEStyleNew(&viewRect, &viewRect);
    gActiveTE = gHideMarkdown ? gHiddenTE : gTE;
    TEActivate(gActiveTE);

    sbRect = viewRect;
    sbRect.left = viewRect.right + (gMarginSize - SCROLLBAR_WIDTH) / 2;
    sbRect.right = sbRect.left + SCROLLBAR_WIDTH;
    sbRect.top -= 1;
    sbRect.bottom += 1;
    gScrollBar = NewControl(gWindow, &sbRect, "\p", false, 0, 0, 0, scrollBarProc, 0);
}

static void DoUpdate(WindowPtr w)
{
    BeginUpdate(w);
    EraseRect(&w->portRect);
    TEUpdate(&w->portRect, gActiveTE);
    DrawControls(w);
    EndUpdate(w);
}

static void DoMenuCommand(long menuResult)
{
    short menuID = HiWord(menuResult);
    short menuItem = LoWord(menuResult);

    if (menuID == mFile) {
        switch (menuItem) {
            case iNew:
                if (ConfirmDiscardChanges())
                    DoNewFile();
                break;
            case iOpen:
                if (ConfirmDiscardChanges())
                    DoOpenFile();
                break;
            case iSave:   DoSave(); break;
            case iSaveAs: DoSaveAs(); break;
            case iQuit:
                if (ConfirmDiscardChanges())
                    gDone = true;
                break;
        }
    } else if (menuID == mEdit) {
        switch (menuItem) {
            case iUndo:      DoUndo(); break;
            case iRedo:      DoRedo(); break;
            case iCut:       DoCut(); break;
            case iCopy:      DoCopy(); break;
            case iPaste:     DoPaste(); break;
            case iSelectAll: DoSelectAll(); break;
        }
    } else if (menuID == mStyle) {
        gDirty = true;
        PushUndoSnapshot();
        gTypingRunActive = false;
        if (gHideMarkdown) {
            switch (menuItem) {
                case iBold:   ToggleFace(bold); break;
                case iItalic: ToggleFace(italic); break;
                case iCode:   ToggleCode(); break;
                case iStrike: break; /* no native strikethrough on classic Mac text styles */
                case iBody:   ToggleHeadingHidden(0); break;
                case iH1:     ToggleHeadingHidden(1); break;
                case iH2:     ToggleHeadingHidden(2); break;
                case iH3:     ToggleHeadingHidden(3); break;
                case iLink:   DoLinkHidden(); break;
                case iNone:   ClearSelectionStyleHidden(); break;
            }
        } else {
            switch (menuItem) {
                case iBold:   WrapSelection("**", "**"); break;
                case iItalic: WrapSelection("*", "*"); break;
                case iCode:   WrapSelection("`", "`"); break;
                case iStrike: WrapSelection("~~", "~~"); break;
                case iBody:   ApplyHeading(0); break;
                case iH1:     ApplyHeading(1); break;
                case iH2:     ApplyHeading(2); break;
                case iH3:     ApplyHeading(3); break;
                case iLink:   DoLink(); break;
                case iNone:   ClearMarkdownInSelection(); break;
            }
            ClearStyles();
        }
        AdjustScrollbar();
    } else if (menuID == mView) {
        switch (menuItem) {
            case iMarkdownView: SetViewMode(false); break;
            case iWriterView:   SetViewMode(true); break;
            case iFont10:
				SetFontSizeIndex(0);
				UpdateFontSizeChecks();
				break;
				
			case iFont12:
				SetFontSizeIndex(1);
				UpdateFontSizeChecks();
				break;

			case iFont14:
				SetFontSizeIndex(2);
				UpdateFontSizeChecks();
				break;

			case iFont16:
				SetFontSizeIndex(3);
				UpdateFontSizeChecks();
				break;

			case iFont18:
				SetFontSizeIndex(4);
				UpdateFontSizeChecks();
				break;

			case iFont20:
				SetFontSizeIndex(5);
				UpdateFontSizeChecks();
				break;
            case iMarginSmall:	ApplyMarginSize(MARGIN_SMALL); UpdateMarginMenuChecks(iMarginSmall); break;
			case iMarginMedium:	ApplyMarginSize(MARGIN_MEDIUM); UpdateMarginMenuChecks(iMarginMedium); break;
			case iMarginLarge:	ApplyMarginSize(MARGIN_LARGE); UpdateMarginMenuChecks(iMarginLarge); break;
        }
    } else if (menuID == mHelp) {
        switch (menuItem) {
            case iAbout: ShowAboutBox(); break;
        }
    } else if (menuID == mFont) {
		switch (menuItem) {
			case iFontChicago:
				SetBodyFontByName("\pChicago", iFontChicago);
				break;

			case iFontGeneva:
				SetBodyFontByName("\pGeneva", iFontGeneva);
				break;

			case iFontHelvetica:
				SetBodyFontByName("\pHelvetica", iFontHelvetica);
				break;

			case iFontNewYork:
				SetBodyFontByName("\pNew York", iFontNewYork);
				break;

			case iFontPalatino:
				SetBodyFontByName("\pPalatino", iFontPalatino);
				break;

			case iFontTimes:
				SetBodyFontByName("\pTimes", iFontTimes);
				break;
		}
	}
    HiliteMenu(0);
    /* HiliteMenu un-hilites the clicked title assuming the Menu Manager's
       own standard white-bar/black-text look, which clobbers our inverted
       Writer-mode bar -- reassert it now that the menu has closed. */
    UpdateMenuBarLook();
}

static void EventLoop(void)
{
    EventRecord event;
    WindowPtr w;
    short part;

    while (!gDone) {
        if (WaitNextEvent(everyEvent, &event, 15, NULL)) {
            /* Disposing a dialog/window doesn't restore the caller's port
               -- cheap insurance against any path (found or not) leaving
               thePort dangling at a freed window's memory. */
            SetPort(gWindow);
            switch (event.what) {
                case updateEvt:
                    DoUpdate((WindowPtr) event.message);
                    break;

                case mouseDown:
                    part = FindWindow(event.where, &w);
                    if (part == inMenuBar) {
                        UpdateEditMenuState();
                        DoMenuCommand(MenuSelect(event.where));
                    } else if (part == inContent) {
                        ControlHandle hitControl;

                        SetPort(w);
                        GlobalToLocal(&event.where);
                        if (FindControl(event.where, w, &hitControl) != 0 && hitControl == gScrollBar)
                            DoScrollClick(event.where);
                        else {
                            gTypingRunActive = false;
                            TEClick(event.where, (event.modifiers & shiftKey) != 0, gActiveTE);
                        }
                    }
                    break;

                case keyDown:
                case autoKey: {
                    char key = event.message & charCodeMask;
                    Boolean isContentKey = (key < 0x1C || key > 0x1F);

                    if (event.modifiers & cmdKey) {
                        if (event.what == keyDown) {
                            if ((key == 'z' || key == 'Z') && (event.modifiers & shiftKey))
                                DoRedo();
                            else {
                                UpdateEditMenuState();
                                DoMenuCommand(MenuKey(key));
                            }
                        }
                    } else {
                        if (isContentKey) {
                            if (!gTypingRunActive) {
                                PushUndoSnapshot();
                                gTypingRunActive = true;
                            }
                        } else {
                            gTypingRunActive = false;
                        }

                        TEKey(key, gActiveTE);
                        if (isContentKey) {
                            gDirty = true;
                            if (gHideMarkdown)
                                DetectInlineMarkdown(key);
                        }
                        ScrollCaretIntoView();
                        UpdateScrollbarRange();
                    }
                    break;
                }

                case activateEvt:
                    if ((event.modifiers & activeFlag) != 0)
                        TEActivate(gActiveTE);
                    else
                        TEDeactivate(gActiveTE);
                    break;
            }
        }
        TEIdle(gActiveTE);
    }
}


int main(void)
{
    short message, count;

    Init();
    LoadZoomPref();
    MakeMenu();
    UpdateFontSizeChecks();
    MakeWindow();

    /* A newly-created visible window has its whole content area marked
       invalid automatically, but the splash dialog appears before the
       event loop ever gets a chance to dequeue and process that update
       event -- force the real BeginUpdate/TEUpdate/EndUpdate cycle to
       happen now, so the window has gone through one proper paint before
       the user can type anything. Without this, the very first line typed
       (before any other update has occurred) doesn't render reliably. */
    DoUpdate(gWindow);

    CountAppFiles(&message, &count);
    if (count >= 1 && message == appOpen)
        DoStartupOpen();
    else
        ShowSplashScreen();

    EventLoop();
    return 0;
}
