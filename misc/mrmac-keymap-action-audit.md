MRMAC Keymap Action Audit

Entscheidung: Die Macro-Oberfläche verwendet kurze MRMAC-Namen. Lange Keymap-Action-IDs bleiben interne Dispatch-Ziele.
Quelle: keymap/MRKeymapActionCatalog.cpp, app/MRCommandRouter.cpp, mrmac/MRVM.cpp.

Action-ID                         	Label                     	Dispatch                                    	Existing       	Canonical MRMAC          	Tranche              	Status
----------------------------------	--------------------------	--------------------------------------------	---------------	-------------------------	---------------------	------
MRMAC_CURSOR_LEFT                 	Cursor left               	EditorCommand:cmCharLeft                    	LEFT           	LEFT;                    	Z1 simple dispatcher 	keep
MRMAC_CURSOR_RIGHT                	Cursor right              	EditorCommand:cmCharRight                   	RIGHT          	RIGHT;                   	Z1 simple dispatcher 	keep
MRMAC_CURSOR_UP                   	Cursor up                 	EditorCommand:cmLineUp                      	UP             	UP;                      	Z1 simple dispatcher 	keep
MRMAC_CURSOR_DOWN                 	Cursor down               	EditorCommand:cmLineDown                    	DOWN           	DOWN;                    	Z1 simple dispatcher 	keep
MRMAC_CURSOR_HOME                 	Cursor to home            	EditorCommand:cmLineStart                   	HOME           	HOME;                    	Z1 simple dispatcher 	keep
MRMAC_CURSOR_END_OF_LINE          	Cursor to end of line     	EditorCommand:cmLineEnd                     	EOL            	EOL;                     	Z1 simple dispatcher 	keep
MRMAC_VIEW_PAGE_UP                	Display page up           	EditorCommand:cmPageUp                      	PAGE_UP        	PAGE_UP;                 	Z1 simple dispatcher 	keep
MRMAC_VIEW_PAGE_DOWN              	Display page down         	EditorCommand:cmPageDown                    	PAGE_DOWN      	PAGE_DOWN;               	Z1 simple dispatcher 	keep
MRMAC_CURSOR_TOP_OF_FILE          	To top of the file        	EditorCommand:cmTextStart                   	TOF            	TOF;                     	Z1 simple dispatcher 	keep
MRMAC_CURSOR_BOTTOM_OF_FILE       	To bottom of the file     	EditorCommand:cmTextEnd                     	EOF            	EOF;                     	Z1 simple dispatcher 	keep
MRMAC_CURSOR_NEXT_PAGE_BREAK      	Cursor to next page break 	Custom:MoveCursorToNextPageBreak            	NEXT_PAGE_BREAK	NEXT_PAGE_BREAK;         	Z1 simple dispatcher 	keep
MRMAC_CURSOR_PREV_PAGE_BREAK      	Cursor to last page break 	Custom:MoveCursorToPrevPageBreak            	LAST_PAGE_BREAK	LAST_PAGE_BREAK;         	Z1 simple dispatcher 	keep
MRMAC_CURSOR_WORD_LEFT            	Cursor word left          	EditorCommand:cmWordLeft                    	WORD_LEFT      	WORD_LEFT;               	Z1 simple dispatcher 	keep
MRMAC_CURSOR_WORD_RIGHT           	Cursor word right         	EditorCommand:cmWordRight                   	WORD_RIGHT     	WORD_RIGHT;              	Z1 simple dispatcher 	keep
MRMAC_CURSOR_TOP_OF_WINDOW        	Top of window             	WindowMethod:CursorTopOfWindow              	-              	TOP_OF_WINDOW;           	Z1 simple dispatcher 	add
MRMAC_CURSOR_BOTTOM_OF_WINDOW     	Bottom of window          	WindowMethod:CursorBottomOfWindow           	-              	BOTTOM_OF_WINDOW;        	Z1 simple dispatcher 	add
MRMAC_VIEW_SCROLL_UP              	Scroll window up          	Custom:ScrollWindowUp                       	-              	SCROLL_UP;               	Z1 simple dispatcher 	add
MRMAC_VIEW_SCROLL_DOWN            	Scroll window down        	Custom:ScrollWindowDown                     	-              	SCROLL_DOWN;             	Z1 simple dispatcher 	add
MRMAC_CURSOR_START_OF_BLOCK       	Cursor to start of block  	WindowMethod:CursorStartOfBlock             	-              	START_OF_BLOCK;          	Z1 simple dispatcher 	add
MRMAC_CURSOR_END_OF_BLOCK         	Cursor to end of block    	WindowMethod:CursorEndOfBlock               	-              	END_OF_BLOCK;            	Z1 simple dispatcher 	add
MRMAC_CURSOR_GOTO_LINE            	Move cursor to line num   	AppCommand:cmMrSearchGotoLineNumber         	GOTO_LINE      	GOTO_LINE;               	Z3 dialog/interactive	keep
MRMAC_CURSOR_INDENT               	Indent                    	Custom:CursorIndent                         	INDENT         	INDENT;                  	Z1 simple dispatcher 	keep
MRMAC_CURSOR_TAB_RIGHT            	Tab right                 	Custom:CursorTabRight                       	TAB_RIGHT      	TAB_RIGHT;               	Z1 simple dispatcher 	keep
MRMAC_CURSOR_TAB_LEFT             	Tab left                  	Custom:CursorTabLeft                        	TAB_LEFT       	TAB_LEFT;                	Z1 simple dispatcher 	keep
MRMAC_CURSOR_UNDENT               	Undent                    	Custom:CursorUndent                         	UNDENT         	UNDENT;                  	Z1 simple dispatcher 	keep
MRMAC_MARK_PUSH_POSITION          	Mark position on stack    	AppCommand:cmMrSearchPushMarker             	MARK_POS       	MARK_POS;                	Z1 simple dispatcher 	keep
MRMAC_MARK_POP_POSITION           	Get position from stack   	AppCommand:cmMrSearchGetMarker              	POP_MARK       	POP_MARK;                	Z1 simple dispatcher 	keep
MRMAC_MARK_SET_RANDOM_ACCESS      	Set a random access mark  	Custom:SetRandomAccessMark                  	-              	SET_RANDOM_MARK(n);      	Z2 sequence-dependent	added
MRMAC_VIEW_CENTER_LINE            	Center line on screen     	WindowMethod:ViewCenterLine                 	-              	CENTER_LINE_ON_SCREEN;   	Z1 simple dispatcher 	add
MRMAC_MARK_GET_RANDOM_ACCESS      	Get random access mark    	Custom:GetRandomAccessMark                  	-              	GET_RANDOM_MARK(n);      	Z2 sequence-dependent	added
MRMAC_DELETE_TO_EOL               	Delete to end of line     	EditorCommand:cmDelEnd                      	-              	DEL_EOL;                 	Z1 simple dispatcher 	add
MRMAC_DELETE_FORWARD_CHAR_OR_BLOCK	Del character (or block)  	Custom:DeleteForwardCharOrBlock             	-              	DEL_CHAR_OR_BLOCK;       	Z1 simple dispatcher 	add
MRMAC_DELETE_FORWARD_WORD         	Delete word forward       	EditorCommand:cmDelWord                     	-              	DEL_WORD;                	Z1 simple dispatcher 	add
MRMAC_DELETE_BACKWARD_CHAR        	Back space                	EditorCommand:cmBackSpace                   	BACK_SPACE     	BACK_SPACE;              	Z1 simple dispatcher 	keep
MRMAC_DELETE_BACKWARD_WORD        	Backspace a whole word    	EditorCommand:cmDelWordLeft                 	-              	BACK_WORD;               	Z1 simple dispatcher 	add
MRMAC_DELETE_LINE                 	Delete line               	EditorCommand:cmDelLine                     	DEL_LINE       	DEL_LINE;                	Z1 simple dispatcher 	keep
MRMAC_DELETE_BACKWARD_TO_HOME     	Backspace to home         	EditorCommand:cmDelStart                    	-              	BACK_HOME;               	Z1 simple dispatcher 	add
MRMAC_UNDO                        	Undo                      	AppCommand:cmMrEditUndo                     	-              	UNDO;                    	Z1 simple dispatcher 	add
MRMAC_REDO_LAST_UNDO              	Undo your last undo       	AppCommand:cmMrEditRedo                     	-              	REDO;                    	Z1 simple dispatcher 	add
MRMAC_SEARCH_FORWARD              	Search                    	AppCommand:cmMrSearchFindText               	-              	SEARCH;                  	Z3 dialog/interactive	add
MRMAC_SEARCH_REPLACE              	Search and replace        	AppCommand:cmMrSearchReplace                	-              	SEARCH_REPLACE;          	Z3 dialog/interactive	add
MRMAC_SEARCH_REPEAT_LAST          	Repeat last Search/Replace	AppCommand:cmMrSearchRepeatPrevious         	-              	REPEAT_SEARCH;           	Z1 simple dispatcher 	add
MRMAC_SEARCH_MULTI_FILE           	Multi file search         	AppCommand:cmMrSearchMultiFileSearch        	-              	MULTI_FILE_SEARCH;       	Z3 dialog/interactive	add
MRMAC_SEARCH_LIST_MATCHED_FILES   	List matched files        	AppCommand:cmMrSearchListFilesFromLastSearch	-              	LIST_MATCHED_FILES;      	Z3 dialog/interactive	add
MRMAC_BLOCK_COPY_TO_CLIPBOARD     	Copy to MS Windows        	Custom:CopyMarkedBlockToSystemClipboard     	-              	COPY_BLOCK_TO_CLIPBOARD; 	Z1 simple dispatcher 	add
MRMAC_BLOCK_PASTE_FROM_CLIPBOARD  	Paste from MS Windows     	AppCommand:cmMrEditPasteFromBuffer          	-              	PASTE_FROM_CLIPBOARD;    	Z1 simple dispatcher 	add
MRMAC_BLOCK_MARK_STREAM           	Mark a stream block       	AppCommand:cmMrBlockMarkStream              	-              	STR_BLOCK_BEGIN;         	Z1 simple dispatcher 	add
MRMAC_BLOCK_SET_BEGIN             	Set block begin           	WindowMethod:BlockSetBegin                  	STR_BLOCK_BEGIN	STR_BLOCK_BEGIN;         	Z1 simple dispatcher 	keep
MRMAC_BLOCK_SET_END               	Set block end             	WindowMethod:BlockSetEnd                    	BLOCK_END      	BLOCK_END;               	Z1 simple dispatcher 	keep
MRMAC_BLOCK_SET_COLUMN_BEGIN      	Set column block begin    	WindowMethod:BlockSetColumnBegin            	COL_BLOCK_BEGIN	COL_BLOCK_BEGIN;         	Z1 simple dispatcher 	keep
MRMAC_BLOCK_MARK_WORD_RIGHT       	Mark word right           	WindowMethod:BlockMarkWordRight             	-              	MARK_WORD_RIGHT;         	Z1 simple dispatcher 	add
MRMAC_BLOCK_CLEAR                 	Turn block mark off       	WindowMethod:BlockClear                     	BLOCK_OFF      	BLOCK_OFF;               	Z1 simple dispatcher 	keep
MRMAC_BLOCK_UNDENT                	Undent block              	AppCommand:cmMrBlockUndent                  	-              	UNDENT_BLOCK;            	Z1 simple dispatcher 	add
MRMAC_BLOCK_INDENT                	Indent block              	AppCommand:cmMrBlockIndent                  	-              	INDENT_BLOCK;            	Z1 simple dispatcher 	add
MRMAC_BLOCK_COPY                  	Copy the marked block     	AppCommand:cmMrBlockCopy                    	COPY_BLOCK     	COPY_BLOCK;              	Z1 simple dispatcher 	keep
MRMAC_BLOCK_MOVE                  	Move marked block         	AppCommand:cmMrBlockMove                    	MOVE_BLOCK     	MOVE_BLOCK;              	Z1 simple dispatcher 	keep
MRMAC_BLOCK_DELETE                	Delete marked block       	AppCommand:cmMrBlockDelete                  	DELETE_BLOCK   	DELETE_BLOCK;            	Z1 simple dispatcher 	keep
MRMAC_BLOCK_COPY_INTERWINDOW      	Interwindow block copy    	AppCommand:cmMrBlockWindowCopy              	-              	WINDOW_COPY_BLOCK;       	Z3 dialog/interactive	add
MRMAC_BLOCK_MOVE_INTERWINDOW      	Interwindow block move    	AppCommand:cmMrBlockWindowMove              	-              	WINDOW_MOVE_BLOCK;       	Z3 dialog/interactive	add
MRMAC_BLOCK_MOVE_TO_BUFFER        	Move block to buffer      	AppCommand:cmMrEditCutToBuffer              	-              	CUT_BLOCK;               	Z1 simple dispatcher 	add
MRMAC_BLOCK_APPEND_TO_BUFFER      	Append block to buffer    	AppCommand:cmMrEditAppendToBuffer           	-              	APPEND_BLOCK;            	Z1 simple dispatcher 	add
MRMAC_BLOCK_CUT_APPEND_TO_BUFFER  	Cut and Append Block      	AppCommand:cmMrEditCutAndAppendToBuffer     	-              	CUT_APPEND_BLOCK;        	Z1 simple dispatcher 	add
MRMAC_BLOCK_COPY_FROM_BUFFER      	Copy block from buffer    	AppCommand:cmMrEditPasteFromBuffer          	-              	PASTE_BLOCK;             	Z1 simple dispatcher 	add
MRMAC_BLOCK_MATH                  	Perform math on block     	Custom:BlockMath                            	-              	BLOCK_MATH;              	Z1 simple dispatcher 	add
MRMAC_BLOCK_EXTEND_BY_MOTION      	Shift cursor block mark   	Custom:ExtendBlockByMotion                  	-              	EXTEND_BLOCK_BY_MOTION(s);	Z2 sequence-dependent	added
MRMAC_FILE_SAVE                   	Save file                 	AppCommand:cmMrFileSave                     	SAVE_FILE      	SAVE_FILE;               	Z1 simple dispatcher 	keep
MR_FILE_SAVE_ALL                  	Save all dirty files      	AppCommand:cmMrFileSaveAll                  	-              	SAVE_ALL;                	Z1 simple dispatcher 	add
MR_FILE_REVERT                    	Revert file               	AppCommand:cmMrFileRevert                   	-              	REVERT_FILE;             	Z3 dialog/interactive	add
MR_SAVE_BLOCK_TO_FILE             	Save block to file        	AppCommand:cmMrBlockSaveToDisk              	SAVE_BLOCK     	SAVE_BLOCK;              	Z3 dialog/interactive	keep
MR_LOAD_BLOCK_FROM_FILE           	Load block from file      	Custom:LoadBlockFromFile                    	-              	LOAD_BLOCK;              	Z3 dialog/interactive	add
MR_TEXT_CENTER_LINE               	Center current line       	Custom:CenterLine                           	-              	CENTER_LINE;             	Z1 simple dispatcher 	add
MR_TEXT_REFORMAT_PARAGRAPH        	Reformat paragraph        	Custom:ReformatParagraph                    	-              	REFORMAT_PARAGRAPH;      	Z1 simple dispatcher 	add
MR_TEXT_REFORMAT_DOCUMENT         	Reformat document         	Custom:ReformatDocument                     	-              	REFORMAT_DOCUMENT;       	Z1 simple dispatcher 	add
MR_TOGGLE_FORMAT_RULER            	Toggle format ruler       	Custom:ToggleFormatRuler                    	-              	TOGGLE_FORMAT_RULER;     	Z1 simple dispatcher 	add
MR_TOGGLE_WORD_WRAP               	Toggle word wrap          	Custom:ToggleWordWrap                       	-              	TOGGLE_WORD_WRAP;        	Z1 simple dispatcher 	add
MR_SET_LEFT_MARGIN                	Set left margin           	Custom:SetLeftMargin                        	-              	SET_LEFT_MARGIN;         	Z3 dialog/interactive	add
MR_SET_RIGHT_MARGIN               	Set right margin          	Custom:SetRightMargin                       	-              	SET_RIGHT_MARGIN;        	Z3 dialog/interactive	add
MR_JUSTIFY_PARAGRAPH              	Justify paragraph         	Custom:JustifyParagraph                     	-              	JUSTIFY_PARAGRAPH;       	Z1 simple dispatcher 	add
MR_SORT_COLUMN_BLOCK_TOGGLE       	Sort marked column block  	Custom:SortColumnBlockToggle                	-              	SORT_COLUMN_BLOCK_TOGGLE;	Z1 simple dispatcher 	add
MR_FILE_FORCE_SAVE                	Force save                	Custom:ForceSave                            	-              	FORCE_SAVE;              	Z1 simple dispatcher 	add
MR_EXIT_DIRTY_SAVE_ALL            	Exit with save-all dialog 	Custom:ExitDirtySaveAll                     	-              	EXIT_SAVE_ALL;           	Z3 dialog/interactive	add
MR_SEARCH_RESULTS_NEXT            	Next search result        	Custom:SearchResultsNext                    	-              	NEXT_SEARCH_RESULT;      	Z1 simple dispatcher 	add
