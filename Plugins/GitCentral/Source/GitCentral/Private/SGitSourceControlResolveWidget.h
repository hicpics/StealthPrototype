// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"
#include "ISourceControlState.h"

class SMultiLineEditableTextBox;
class SWindow;

//-------------------------------------
// Source Control Window Constants
//-------------------------------------
namespace EResolveResults
{
	enum Type
	{
		RESOLVE_ACCEPTED,
		RESOLVE_CANCELED
	};
}

class FResolveItem : public TSharedFromThis<FResolveItem>
{
public:
	/** Constructor */
	explicit FResolveItem(const FSourceControlStateRef& InItem);

	/** Returns the full path of the item in source control */
	FString GetFilename() const { return Item->GetFilename(); }

	/** Returns the name of the item as displayed in the widget */
	FText GetDisplayName() const { return DisplayName; }

	/** Returns the name of the icon to be used in the list item widget */
	FName GetIconName() const { return Item->GetSmallIconName(); }

	/** Returns the tooltip text for the icon */
	FText GetIconTooltip() const { return Item->GetDisplayTooltip(); }

	enum EResolveOption
	{
		None,
		Yours,
		Theirs
	};

	void CheckYours(const ECheckBoxState CheckBoxState);
	void CheckTheirs(const ECheckBoxState CheckBoxState);
	ECheckBoxState IsCheckedYours() const;
	ECheckBoxState IsCheckedTheirs() const;

	/** Returns the checkbox state of this item */
	EResolveOption GetResolveOption() const { return ResolveOption; }

	/** Sets the checkbox state of this item */
	void SetResolveOption(EResolveOption Option) { ResolveOption = Option; }

private:
	/** Shared pointer to the source control state object itself */
	FSourceControlStateRef Item;

	/** Checkbox state */
	EResolveOption ResolveOption;

	/** Cached name to display in the listview */
	FText DisplayName;
};

class SSourceControlResolveWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSourceControlResolveWidget)
		: _ParentWindow()
		, _Items()
	{}

		SLATE_ATTRIBUTE(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ATTRIBUTE(TArray<FSourceControlStateRef>, Items)

	SLATE_END_ARGS()

	/** Constructor */
	SSourceControlResolveWidget()
	{
	}

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Get dialog result */
	EResolveResults::Type GetResult() { return DialogResult; }

	void GetFilenamesForResolveYours(TArray<FString>& OutFilenames) const;
	void GetFilenamesForResolveTheirs(TArray<FString>& OutFilenames) const;

private:

	/** Called when the settings of the dialog are to be accepted*/
	FReply OKClicked();

	/** Called when the settings of the dialog are to be ignored*/
	FReply CancelClicked();

	/** Called to check if the OK button is enabled or not. */
	bool IsOKEnabled() const;

	/** Called by SListView to get a widget corresponding to the supplied item */
	TSharedRef<ITableRow> OnGenerateRowForList(TSharedPtr<FResolveItem> ResolveItemData, const TSharedRef<STableViewBase>& OwnerTable);

	/**
	 * Returns the current column sort mode (ascending or descending) if the ColumnId parameter matches the current
	 * column to be sorted by, otherwise returns EColumnSortMode_None.
	 *
	 * @param	ColumnId	Column ID to query sort mode for.
	 *
	 * @return	The sort mode for the column, or EColumnSortMode_None if it is not known.
	 */
	EColumnSortMode::Type GetColumnSortMode(const FName ColumnId) const;

	/**
	 * Callback for SHeaderRow::Column::OnSort, called when the column to sort by is changed.
	 *
	 * @param	ColumnId	The new column to sort by
	 * @param	InSortMode	The sort mode (ascending or descending)
	 */
	void OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode);

	/**
	 * Requests that the source list data be sorted according to the current sort column and mode,
	 * and refreshes the list view.
	 */
	void RequestSort();

	/**
	 * Sorts the source list data according to the current sort column and mode.
	 */
	void SortTree();

	ECheckBoxState IsAllYoursChecked() const;
	ECheckBoxState IsAllTheirsChecked() const;
	void CheckAllYours(const ECheckBoxState CheckBoxState);
	void CheckAllTheirs(const ECheckBoxState CheckBoxState);

private:
	EResolveResults::Type DialogResult;

	/** ListBox for selecting which object to consolidate */
	TSharedPtr<SListView<TSharedPtr<FResolveItem>>> ListView;

	/** Collection of objects (Widgets) to display in the List View. */
	TArray<TSharedPtr<FResolveItem>> ListViewItems;

	/** Pointer to the parent modal window */
	TWeakPtr<SWindow> ParentFrame;

	/** Specify which column to sort with */
	FName SortByColumn;

	/** Currently selected sorting mode */
	EColumnSortMode::Type SortMode;
};

class SSourceControlResolveListRow : public SMultiColumnTableRow<TSharedPtr<FResolveItem>>
{
public:

	SLATE_BEGIN_ARGS(SSourceControlResolveListRow) {}

	/** The SSourceControlResolveWidget that owns the tree.  We'll only keep a weak reference to it. */
	SLATE_ARGUMENT(TSharedPtr<SSourceControlResolveWidget>, SourceControlResolveWidget)

		/** The list item for this row */
		SLATE_ARGUMENT(TSharedPtr<FResolveItem>, Item)

	SLATE_END_ARGS()

	/** Construct function for this widget */
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView);

	/** Overridden from SMultiColumnTableRow.  Generates a widget for this column of the list row. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:

	/** Weak reference to the SSourceControlResolveWidget that owns our list */
	TWeakPtr<SSourceControlResolveWidget> SourceControlResolveWidgetPtr;

	/** The item associated with this row of data */
	TSharedPtr<FResolveItem> Item;
};
