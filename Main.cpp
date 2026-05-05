# include <Siv3D.hpp>

// --- データ構造定義 ---
struct Note {
	String title;
	TextAreaEditState state;
	ColorF color;
	double fontSize;
	size_t fontStyleIdx;
};

// --- ヘルパー関数：JSON保存 ---
void SaveAppData(const Array<Note>& notes, size_t currentIndex, const String& path) {
	JSON json;
	json[U"lastOpenedIndex"] = currentIndex;
	for (const auto& note : notes) {
		JSON obj;
		obj[U"title"] = note.title;
		obj[U"content"] = note.state.text;
		obj[U"color"] = note.color.toVec4();
		obj[U"fontSize"] = note.fontSize;
		obj[U"fontStyleIdx"] = note.fontStyleIdx;
		json[U"notes"].push_back(obj);
	}
	json.save(path);
}

// --- ヘルパー関数：JSON読み込み ---
void LoadAppData(Array<Note>& notes, size_t& currentIndex, const String& path) {
	const JSON json = JSON::Load(path);
	if (not json) return;
	notes.clear();
	for (const auto& obj : json[U"notes"].arrayView()) {
		notes.push_back({
			obj[U"title"].get<String>(),
			TextAreaEditState{ obj[U"content"].get<String>() },
			obj[U"color"].get<ColorF>(),
			obj[U"fontSize"].getOpt<double>().value_or(20.0),
			obj[U"fontStyleIdx"].getOpt<size_t>().value_or(0)
		});
	}
	if (json.hasElement(U"lastOpenedIndex")) {
		currentIndex = Min(json[U"lastOpenedIndex"].get<size_t>(), notes.empty() ? 0 : notes.size() - 1);
	}
}

void Main() {
	Window::SetTitle(U"Siv3D Advanced Note");
	const Font uiFont{ 16, Typeface::Medium };
	const String savePath = U"note_data.json";

	Array<Note> notes;
	size_t currentTabIndex = 0;
	LoadAppData(notes, currentTabIndex, savePath);
	if (notes.empty()) notes.push_back({ U"新規ノート", TextAreaEditState{}, Palette::Skyblue, 20.0, 0 });

	TextEditState searchEditState, titleEditState;
	String searchKeyword, notification;
	Optional<size_t> editingTabIndex, pendingDeleteIdx;
	Stopwatch notificationTimer, undoTimer, autoSaveTimer;
	Optional<Note> lastDeletedNote;
	size_t lastDeletedIdx = 0;
	bool needsSave = false;

	while (System::Update()) {
		const auto& currentNote = notes[currentTabIndex];
		const double luminance = (0.2126 * currentNote.color.r) + (0.7152 * currentNote.color.g) + (0.0722 * currentNote.color.b);
		const bool isDialogOpen = pendingDeleteIdx.has_value();
		const ColorF textColor = (luminance > 0.5) ? Palette::Black : Palette::White;

		Scene::SetBackground(currentNote.color);

		// --- 1. 保存・追加ボタンエリア ---
		uiFont(U"🔍").draw(20, 15, textColor);
		if (SimpleGUI::TextBox(searchEditState, Vec2{ 50, 10 }, 200) && !isDialogOpen) searchKeyword = searchEditState.text;

		// 追加ボタン
		if (SimpleGUI::Button(U"＋ 新規作成", Vec2{ 260, 10 }, 120) && !isDialogOpen) {
			notes.push_back({ U"新規", TextAreaEditState{}, HSV{Random(360.0), 0.4, 0.9}, 20.0, 0 });
			currentTabIndex = notes.size() - 1;
			needsSave = true;
		}

		// 保存ボタン (追加箇所)
		if (SimpleGUI::Button(U"💾 保存", Vec2{ 390, 10 }, 100) && !isDialogOpen) {
			SaveAppData(notes, currentTabIndex, savePath);
			notification = U"保存しました";
			notificationTimer.restart();
			needsSave = false;
		}

		// --- 2. ショートカットキー ---
		if (!isDialogOpen) {
			if ((KeyControl + KeyS).down()) {
				SaveAppData(notes, currentTabIndex, savePath);
				notification = U"保存しました";
				notificationTimer.restart();
				needsSave = false;
			}
		}

		// --- 3. タブの描画 ---
		const double tabW = 140, tabH = 40;
		const Vec2 startPos{ 20, 60 };
		for (int32 i = 0; i < (int32)notes.size(); ++i) {
			const RectF tabRect{ startPos.x + (i * (tabW + 5)), startPos.y, tabW, tabH };
			const bool isMatch = searchKeyword.isEmpty() || notes[i].title.includes(searchKeyword) || notes[i].state.text.includes(searchKeyword);
			const bool isCurrent = (currentTabIndex == i);
			const double alpha = isMatch ? 1.0 : 0.2;

			tabRect.draw(isCurrent ? ColorF{ 1,1,1,0.3 * alpha } : ColorF{ 0,0,0,0.1 * alpha })
				.drawFrame(isCurrent ? 2 : 1, 0, textColor.withAlpha(alpha));

			// 削除ボタン (×)
			const RectF closeBtn{ tabRect.x + tabW - 25, tabRect.y + 10, 20, 20 };
			if (!isDialogOpen && closeBtn.mouseOver()) {
				Cursor::RequestStyle(CursorStyle::Hand);
				if (MouseL.down()) pendingDeleteIdx = i;
			}
			uiFont(U"×").drawAt(closeBtn.center(), textColor.withAlpha(alpha));

			// タイトル表示
			const RectF titleArea{ tabRect.x, tabRect.y, tabW - 30, tabH };
			if (editingTabIndex == (size_t)i) {
				if (SimpleGUI::TextBox(titleEditState, titleArea.pos, titleArea.w)) editingTabIndex.reset();
				notes[i].title = titleEditState.text;
				needsSave = true;
			}
			else {
				uiFont(notes[i].title).drawAt(titleArea.center(), textColor.withAlpha(isMatch ? 1.0 : 0.4));
				if (!isDialogOpen && titleArea.leftClicked()) currentTabIndex = i;
				if (!isDialogOpen && titleArea.mouseOver() && MouseL.down()) {
					editingTabIndex = i;
					titleEditState.text = notes[i].title;
				}
			}
		}

		// --- 4. メイン編集エリア ---
		if (currentTabIndex < notes.size()) {
			if (SimpleGUI::TextArea(notes[currentTabIndex].state, Vec2{ 20, 110 }, SizeF{ 530, 460 })) needsSave = true;

			const double pX = 570;
			HSV hsv = notes[currentTabIndex].color;
			if (SimpleGUI::ColorPicker(hsv, Vec2{ pX, 110 })) {
				notes[currentTabIndex].color = hsv;
				needsSave = true;
			}
			uiFont(U"サイズ").draw(pX, 320, textColor);
			if (SimpleGUI::Slider(notes[currentTabIndex].fontSize, 10.0, 60.0, Vec2{ pX, 350 }, 160)) needsSave = true;
		}

		// --- 5. 自動保存 ---
		if (needsSave && autoSaveTimer.sF() > 2.0) {
			SaveAppData(notes, currentTabIndex, savePath);
			needsSave = false;
			autoSaveTimer.reset();
		}
		else if (needsSave) {
			autoSaveTimer.resume();
		}

		// --- 6. 削除ダイアログとUndo ---
		if (pendingDeleteIdx) {
			Scene::Rect().draw(ColorF{ 0, 0.5 });
			const RectF dlg{ Arg::center = Scene::Center(), 300, 140 };
			dlg.draw(Palette::White).drawFrame(2, 0, Palette::Gray);
			uiFont(U"「{}」を削除しますか？"_fmt(notes[*pendingDeleteIdx].title)).drawAt(dlg.center().movedBy(0, -25), Palette::Black);

			if (SimpleGUI::Button(U"はい", dlg.center().movedBy(-60, 30), 80)) {
				lastDeletedNote = notes[*pendingDeleteIdx];
				lastDeletedIdx = *pendingDeleteIdx;
				undoTimer.restart();

				notes.erase(notes.begin() + *pendingDeleteIdx);
				if (notes.empty()) {
					notes.push_back({ U"新規ノート", TextAreaEditState{}, Palette::Skyblue, 20.0, 0 });
				}
				currentTabIndex = Min<size_t>(currentTabIndex, notes.size() - 1);
				pendingDeleteIdx.reset();
				SaveAppData(notes, currentTabIndex, savePath); // 削除直後に保存
			}
			if (SimpleGUI::Button(U"キャンセル", dlg.center().movedBy(60, 30), 100)) {
				pendingDeleteIdx.reset();
			}
		}

		// Undo（元に戻す）通知
		if (undoTimer.isRunning() && undoTimer.sF() < 5.0) {
			const RectF bar{ Arg::center = Vec2{ Scene::Center().x, Scene::Height() - 40 }, 320, 45 };
			bar.draw(ColorF{ 0.2, 0.95 }).drawFrame(1, 0, Palette::White);
			uiFont(U"ノートを削除しました").draw(bar.pos.movedBy(15, 10), Palette::White);
			if (SimpleGUI::Button(U"元に戻す", bar.pos.movedBy(210, 7), 100)) {
				notes.insert(notes.begin() + lastDeletedIdx, *lastDeletedNote);
				currentTabIndex = lastDeletedIdx;
				undoTimer.reset();
				SaveAppData(notes, currentTabIndex, savePath);
			}
		}

		// 保存完了通知の描画
		if (notificationTimer.isRunning() && notificationTimer.sF() < 2.0) {
			uiFont(notification).drawAt(Scene::Center().x, 30, Palette::Orange.withAlpha(1.0 - Max(0.0, notificationTimer.sF() - 1.0)));
		}
	}
	SaveAppData(notes, currentTabIndex, savePath);
}
