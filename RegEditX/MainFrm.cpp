// MainFrm.cpp : implmentation of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"
#include "AboutDlg.h"
#include "View.h"
#include "MainFrm.h"
#include "CreateNewKeyCommand.h"
#include "NewKeyDlg.h"
#include "RenameKeyCommand.h"
#include "SecurityInformation.h"
#include "ClipboardHelper.h"

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg) {
	if (m_view.PreTranslateMessage(pMsg))
		return TRUE;

	return (CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg));
}

BOOL CMainFrame::OnIdle() {
	UpdateUI();
	UIUpdateToolBar();

	return FALSE;
}

void CMainFrame::UpdateUI() {
	UIEnable(ID_EDIT_UNDO, m_CmdMgr.CanUndo());
	UIEnable(ID_EDIT_REDO, m_CmdMgr.CanRedo());
	BOOL canDelete = m_AllowModify;
	if (m_splitter.GetActivePane() == 0)
		UIEnable(ID_EDIT_DELETE, canDelete = canDelete && m_SelectedNode && m_SelectedNode->CanDelete());
	else {
		UIEnable(ID_EDIT_DELETE, canDelete = canDelete && m_view.CanDeleteSelected());
		UIEnable(ID_EDIT_MODIFYVALUE, m_AllowModify && m_view.CanEditValue());
	}
	UIEnable(ID_EDIT_CUT, canDelete);
	UIEnable(ID_EDIT_PASTE, m_AllowModify && CanPaste());
	UIEnable(ID_NEW_KEY, m_AllowModify && m_SelectedNode->GetNodeType() == TreeNodeType::RegistryKey);
	UISetText(ID_EDIT_UNDO, (m_CmdMgr.CanUndo() ? L"Undo " + m_CmdMgr.GetUndoCommand()->GetName() : CString(L"Undo")) + CString(L"\tCtrl+Z"));
	UISetText(ID_EDIT_REDO, (m_CmdMgr.CanRedo() ? L"Redo " + m_CmdMgr.GetRedoCommand()->GetName() : CString(L"Redo")) + CString(L"\tCtrl+Y"));
	UIEnable(ID_EDIT_RENAME, m_AllowModify && canDelete);
	UISetCheck(ID_EDIT_MODIFY, !m_AllowModify);
	UISetCheck(ID_VIEW_KEYSINLISTVIEW, m_view.IsViewKeys());
	for(int id = ID_NEW_DWORDVALUE; id <= ID_NEW_BINARYVALUE; id++)
		UIEnable(id, m_AllowModify);
}

LRESULT CMainFrame::OnTreeContextMenu(int, LPNMHDR, BOOL&) {
	POINT pt;
	::GetCursorPos(&pt);
	POINT screen(pt);
	::ScreenToClient(m_treeview, &pt);
	auto hItem = m_treeview.HitTest(pt, nullptr);
	if (hItem == nullptr)
		return 0;

	auto current = reinterpret_cast<TreeNodeBase*>(m_treeview.GetItemData(hItem));
	if (current == nullptr)
		return 0;

	auto index = current->GetContextMenuIndex();
	if (index < 0)
		return 0;

	CMenu menu;
	menu.LoadMenuW(IDR_CONTEXT);
	m_CmdBar.TrackPopupMenu(menu.GetSubMenu(index), TPM_LEFTALIGN | TPM_RIGHTBUTTON, screen.x, screen.y);

	return 0;
}

UINT CMainFrame::TrackPopupMenu(CMenuHandle menu, int x, int y) {
	return (UINT)m_CmdBar.TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, x, y);
}

LRESULT CMainFrame::OnTreeSelectionChanged(int, LPNMHDR nmhdr, BOOL&) {
	auto item = reinterpret_cast<NMTREEVIEW*>(nmhdr);
	auto node = reinterpret_cast<TreeNodeBase*>(m_treeview.GetItemData(item->itemNew.hItem));
	m_SelectedNode = node;
	m_view.Update(node);

	return 0;
}

LRESULT CMainFrame::OnTreeDeleteItem(int, LPNMHDR nmhdr, BOOL&) {
	auto item = reinterpret_cast<NMTREEVIEW*>(nmhdr);
	auto node = reinterpret_cast<TreeNodeBase*>(m_treeview.GetItemData(item->itemOld.hItem));
	if(node)
		node->Delete();

	return 0;
}

LRESULT CMainFrame::OnBeginRename(int, LPNMHDR, BOOL&) {
	if (!m_AllowModify)
		return TRUE;

	m_Edit = m_treeview.GetEditControl();
	ATLASSERT(m_Edit.IsWindow());
	return 0;
}

LRESULT CMainFrame::OnEndRename(int, LPNMHDR, BOOL&) {
	ATLASSERT(m_Edit.IsWindow());
	if (!m_Edit.IsWindow())
		return 0;

	CString newName;
	m_Edit.GetWindowText(newName);

	if (newName.CompareNoCase(m_SelectedNode->GetText()) == 0)
		return 0;

	auto cmd = std::make_shared<RenameKeyCommand>(m_SelectedNode->GetFullPath(), newName);
	if (!m_CmdMgr.AddCommand(cmd))
		ShowCommandError(L"Failed to rename key");
	m_Edit.Detach();

	return 0;
}

LRESULT CMainFrame::OnTreeItemExpanding(int, LPNMHDR nmhdr, BOOL&) {
	return m_RegMgr.HandleNotification(nmhdr);

	return 0;
}

LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// create command bar window
	HWND hWndCmdBar = m_CmdBar.Create(m_hWnd, rcDefault, nullptr, ATL_SIMPLE_CMDBAR_PANE_STYLE);
	// attach menu
	m_CmdBar.AttachMenu(GetMenu());
	SetMenu(nullptr);

	UIAddMenu(IDR_MAINFRAME);
	UIAddMenu(IDR_CONTEXT);

	InitCommandBar();

	m_SmallImages.Create(16, 16, ILC_COLOR32 | ILC_MASK, 6, 4);
	m_LargeImages.Create(32, 32, ILC_COLOR32 | ILC_MASK, 6, 4);

	UINT icons[] = {
		IDI_FOLDER_CLOSED, IDI_FOLDER_OPEN, IDI_TEXT, IDI_BINARY,
		IDI_HIVE, IDI_UP, IDI_LAPTOP, IDI_FOLDER_LINK, IDI_LINK
	};

	for (auto id : icons) {
		auto hIcon = AtlLoadIcon(id);
		m_SmallImages.AddIcon(hIcon);
		m_LargeImages.AddIcon(hIcon);
	}

	CToolBarCtrl tb;
	auto hWndToolBar = tb.Create(m_hWnd, nullptr, nullptr, ATL_SIMPLE_TOOLBAR_PANE_STYLE | TBSTYLE_LIST, 0, ATL_IDW_TOOLBAR);
	tb.SetExtendedStyle(TBSTYLE_EX_MIXEDBUTTONS);
	InitToolBar(tb);
	
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	AddSimpleReBarBand(hWndCmdBar);
	AddSimpleReBarBand(hWndToolBar, nullptr, TRUE);

	CReBarCtrl rebar(m_hWndToolBar);
	rebar.LockBands(TRUE);

	CreateSimpleStatusBar();
	CStatusBarCtrl sb(m_hWndStatusBar);

	m_hWndClient = m_splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);

	m_treeview.Create(m_splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
		TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_EDITLABELS, WS_EX_CLIENTEDGE);
	m_treeview.SetExtendedStyle(TVS_EX_DOUBLEBUFFER | 0*TVS_EX_MULTISELECT, 0);
	m_treeview.SetImageList(m_SmallImages, TVSIL_NORMAL);

	m_view.Create(m_splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | LVS_SINGLESEL | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
		LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA | LVS_EDITLABELS, WS_EX_CLIENTEDGE);
	m_view.SetImageList(m_SmallImages, LVSIL_SMALL);
	m_view.SetImageList(m_LargeImages, LVSIL_NORMAL);

	m_splitter.SetSplitterPanes(m_treeview, m_view);
	UpdateLayout();
	m_splitter.SetSplitterPosPct(25);

	UIAddToolBar(hWndToolBar);
	UISetCheck(ID_VIEW_TOOLBAR, 1);
	UISetCheck(ID_VIEW_STATUS_BAR, 1);
	UISetCheck(ID_VIEW_TREEPANE, 1);
	UISetCheck(ID_VIEW_KEYSINLISTVIEW, 1);
	UISetRadioMenuItem(ID_VIEW_TYPE_DETAILS, ID_VIEW_TYPE_DETAILS, ID_VIEW_TYPE_TILES);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != nullptr);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	m_RegMgr.BuildTreeView();
	m_view.Init(&m_RegMgr, this);

	return 0;
}

bool CMainFrame::AddCommand(std::shared_ptr<AppCommandBase> cmd, bool execute) {
	return m_CmdMgr.AddCommand(cmd, execute);
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	m_RegMgr.Destroy();

	// unregister message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != nullptr);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	bHandled = FALSE;
	return 1;
}

LRESULT CMainFrame::OnEditRename(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Edit = m_treeview.EditLabel(m_treeview.GetSelectedItem());

	return 0;
}

LRESULT CMainFrame::OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	PostMessage(WM_CLOSE);
	return 0;
}

LRESULT CMainFrame::OnAlwaysOnTop(WORD, WORD, HWND, BOOL&) {
	auto style = GetWindowLongPtr(GWL_EXSTYLE);
	bool topmost = (style & WS_EX_TOPMOST) == 0;
	RECT dummy;
	SetWindowPos(topmost ? HWND_TOPMOST : HWND_NOTOPMOST, &dummy, SWP_NOREPOSITION | SWP_NOREDRAW | SWP_NOSIZE | SWP_NOMOVE);

	UISetCheck(ID_OPTIONS_ALWAYSONTOP, topmost);
	return 0;
}

LRESULT CMainFrame::OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	static BOOL bVisible = TRUE;	// initially visible
	bVisible = !bVisible;
	CReBarCtrl rebar = m_hWndToolBar;
	int nBandIndex = rebar.IdToIndex(ATL_IDW_BAND_FIRST + 1);	// toolbar is 2nd added band
	rebar.ShowBand(nBandIndex, bVisible);
	UISetCheck(ID_VIEW_TOOLBAR, bVisible);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	BOOL bVisible = !::IsWindowVisible(m_hWndStatusBar);
	::ShowWindow(m_hWndStatusBar, bVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
	UISetCheck(ID_VIEW_STATUS_BAR, bVisible);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CAboutDlg dlg;
	dlg.DoModal();
	return 0;
}

LRESULT CMainFrame::OnEditRedo(WORD, WORD, HWND, BOOL&) {
	ATLASSERT(m_CmdMgr.CanRedo());
	if (!m_CmdMgr.CanRedo())
		return 0;
	m_CmdMgr.Redo();
	m_view.Update(m_SelectedNode, true);

	return 0;
}

LRESULT CMainFrame::OnEditUndo(WORD, WORD, HWND, BOOL&) {
	ATLASSERT(m_CmdMgr.CanUndo());
	if (!m_CmdMgr.CanUndo())
		return 0;
	m_CmdMgr.Undo();
	m_view.Update(m_SelectedNode, true);

	return 0;
}

LRESULT CMainFrame::OnNewKey(WORD, WORD, HWND, BOOL&) {
	auto hItem = m_treeview.GetSelectedItem();
	auto node = reinterpret_cast<TreeNodeBase*>(m_treeview.GetItemData(hItem));
	ATLASSERT(node);

	CNewKeyDlg dlg;
	if (dlg.DoModal() == IDOK) {
		if (node->FindChild(dlg.GetKeyName())) {
			AtlMessageBox(m_hWnd, L"Key name already exists", IDR_MAINFRAME, MB_ICONERROR);
			return 0;
		}
		auto cmd = std::make_shared<CreateNewKeyCommand>(node->GetFullPath(), dlg.GetKeyName());
		if (!m_CmdMgr.AddCommand(cmd))
			ShowCommandError(L"Failed to create key");

	}

	return 0;
}

LRESULT CMainFrame::OnViewTreePane(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	bool bShow = (m_splitter.GetSinglePaneMode() != SPLIT_PANE_NONE);
	m_splitter.SetSinglePaneMode(bShow ? SPLIT_PANE_NONE : SPLIT_PANE_RIGHT);
	UISetCheck(ID_VIEW_TREEPANE, bShow);

	return 0;
}

LRESULT CMainFrame::OnDelete(WORD, WORD, HWND, BOOL&) {
	// delete key
	ATLASSERT(m_SelectedNode && m_SelectedNode->GetNodeType() == TreeNodeType::RegistryKey);

	return 0;
}

LRESULT CMainFrame::OnEditModify(WORD, WORD, HWND, BOOL&) {
	m_AllowModify = !m_AllowModify;
	UISetCheck(ID_EDIT_MODIFY, !m_AllowModify);

	return 0;
}

LRESULT CMainFrame::OnRefresh(WORD, WORD, HWND, BOOL&) {
	CWaitCursor wait;

	RegistryManager::Get().Refresh();
	m_view.Update(m_SelectedNode, true);

	return 0;
}

LRESULT CMainFrame::OnCopyKeyName(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	ATLASSERT(m_SelectedNode);
	ClipboardHelper::CopyText(*this, m_SelectedNode->GetText());
	return 0;
}

LRESULT CMainFrame::OnCopyFullKeyName(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	ATLASSERT(m_SelectedNode);
	ClipboardHelper::CopyText(*this, m_SelectedNode->GetFullPath());
	return 0;
}

void CMainFrame::ShowCommandError(PCWSTR message) {
	AtlMessageBox(m_hWnd, message ? message : L"Error", IDR_MAINFRAME, MB_ICONERROR);
}

bool CMainFrame::CanPaste() const {
	return false;
}

LRESULT CMainFrame::OnEditPermissions(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_SelectedNode == nullptr || m_SelectedNode->GetNodeType() != TreeNodeType::RegistryKey)
		return 0;

	auto node = static_cast<RegKeyTreeNode*>(m_SelectedNode);
	CSecurityInformation info(*node->GetKey(), node->GetText(), !m_AllowModify);
	::EditSecurity(m_hWnd, &info);

	return 0;
}

void CMainFrame::InitCommandBar() {
	m_CmdBar.m_bAlphaImages = true;

	struct {
		UINT id, icon;
		HICON hIcon = nullptr;
	} cmds[] = {
		{ ID_EDIT_COPY, IDI_COPY },
		{ ID_VIEW_REFRESH, IDI_REFRESH },
		{ ID_APP_ABOUT, IDI_ABOUT },
		{ ID_EDIT_MODIFY, IDI_LOCK },
		{ ID_EDIT_PROPERTIES, IDI_PROPERTIES },
		{ ID_EDIT_PERMISSIONS, IDI_SECURITY },
		{ ID_EDIT_COPY, IDI_COPY },
		{ ID_EDIT_CUT, IDI_CUT },
		{ ID_EDIT_PASTE, IDI_PASTE },
		{ ID_EDIT_DELETE, IDI_DELETE },
		{ ID_EDIT_RENAME, IDI_RENAME },
		{ ID_VIEW_TYPE_DETAILS, IDI_DETAILS },
		{ ID_VIEW_TYPE_LIST, IDI_LIST },
		{ ID_VIEW_TYPE_TILES, IDI_TILES },
		{ ID_VIEW_TYPE_ICONS, IDI_LARGE_ICONS },
	};
	for (const auto& cmd : cmds) {
		m_CmdBar.AddIcon(cmd.icon ? AtlLoadIcon(cmd.icon) : cmd.hIcon, cmd.id);
	}

}

void CMainFrame::InitToolBar(CToolBarCtrl& tb) {
	CImageList tbImages;
	tbImages.Create(24, 24, ILC_COLOR32, 8, 4);
	tb.SetImageList(tbImages);

	struct {
		UINT id;
		int image;
		int style = BTNS_BUTTON;
		PCWSTR text = nullptr;
	} buttons[] = {
		{ ID_EDIT_MODIFY, IDI_LOCK },
		{ 0 },
		{ ID_VIEW_REFRESH, IDI_REFRESH },
		{ 0 },
		{ ID_EDIT_COPY, IDI_COPY },
		{ ID_EDIT_CUT, IDI_CUT },
		{ ID_EDIT_PASTE, IDI_PASTE },
		{ ID_EDIT_DELETE, IDI_DELETE },
		{ 0 },
		{ 101, IDI_ADD, BTNS_BUTTON | BTNS_SHOWTEXT, L"New" },
		{ 0 },
		{ 100, IDI_VIEW, BTNS_BUTTON | BTNS_SHOWTEXT, L"View" },
	};
	for (auto& b : buttons) {
		if (b.id == 0)
			tb.AddSeparator(0);
		else {
			int image = tbImages.AddIcon(AtlLoadIconImage(b.image, 0, 24, 24));
			tb.AddButton(b.id, b.style, TBSTATE_ENABLED, image, b.text, 0);
		}
	}
	AddToolBarDropDownMenu(tb, 101, IDR_NEWVALUE);
	AddToolBarDropDownMenu(tb, 100, IDR_CONTEXT, 3);
}
