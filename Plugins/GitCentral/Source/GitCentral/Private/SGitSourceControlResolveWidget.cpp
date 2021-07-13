// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#include "SGitSourceControlResolveWidget.h"

#include "Misc/MessageDialog.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"
#include "ISourceControlProvider.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SErrorText.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SCheckBox.h"
#include "EditorStyleSet.h"
#include "ISourceControlModule.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "FileHelpers.h"


#if SOURCE_CONTROL_WITH_SLATE

#define LOCTEXT_NAMESPACE "SSourceControlResolve"


namespace SSourceControlResolveWidgetDefs
{
	const FName ColumnID_CheckBoxYoursLabel("Resolve Using Yours");
	const FName ColumnID_CheckBoxTheirsLabel("Resolve Using Theirs");
	const FName ColumnID_IconLabel("Icon");
	const FName ColumnID_FileLabel("File");

	const float CheckBoxColumnWidth = 23.0f;
	const float IconColumnWidth = 21.0f;
}


FResolveItem::FResolveItem(const FSourceControlStateRef& InItem)
	: Item(InItem)
{
	ResolveOption = FResolveItem::None;
	DisplayName = FText::FromString(Item->GetFilename());
}


void FResolveItem::CheckYours(const ECheckBoxState CheckBoxState)
{
	FResolveItem::EResolveOption Option = CheckBoxState == ECheckBoxState::Checked ? FResolveItem::Yours : FResolveItem::None;
	SetResolveOption(Option);
}

void FResolveItem::CheckTheirs(const ECheckBoxState CheckBoxState)
{
	FResolveItem::EResolveOption Option = CheckBoxState == ECheckBoxState::Checked ? FResolveItem::Theirs : FResolveItem::None;
	SetResolveOption(Option);
}

ECheckBoxState FResolveItem::IsCheckedYours() const
{
	return GetResolveOption() == FResolveItem::Yours ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FResolveItem::IsCheckedTheirs() const
{
	return GetResolveOption() == FResolveItem::Theirs ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SSourceControlResolveListRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	SourceControlResolveWidgetPtr = InArgs._SourceControlResolveWidget;
	Item = InArgs._Item;

	SMultiColumnTableRow<TSharedPtr<FResolveItem>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
}


TSharedRef<SWidget> SSourceControlResolveListRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	// Create the widget for this item
	TSharedPtr<SSourceControlResolveWidget> SourceControlResolveWidget = SourceControlResolveWidgetPtr.Pin();
	if (SourceControlResolveWidget.IsValid())
	{
		check(Item.IsValid());

		const FMargin RowPadding(3, 0, 0, 0);

		TSharedPtr<SWidget> ItemContentWidget;

		if(ColumnName == SSourceControlResolveWidgetDefs::ColumnID_CheckBoxYoursLabel)
		{
			ItemContentWidget = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(RowPadding)
				[
					SNew(SCheckBox)
					.IsChecked(Item.Get(), &FResolveItem::IsCheckedYours)
					.OnCheckStateChanged(Item.Get(), &FResolveItem::CheckYours) 
					.ToolTipText(LOCTEXT("CheckBoxResolveYours_Tooltip", "Resolve using Yours"))
				];
		}
		else if(ColumnName == SSourceControlResolveWidgetDefs::ColumnID_CheckBoxTheirsLabel)
		{
			ItemContentWidget = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(RowPadding)
				[
					SNew(SCheckBox)
					.IsChecked(Item.Get(), &FResolveItem::IsCheckedTheirs)
					.OnCheckStateChanged(Item.Get(), &FResolveItem::CheckTheirs)
					.ToolTipText(LOCTEXT("CheckBoxResolveTheirs_Tooltip", "Resolve using Theirs"))
				];
		}
		else if(ColumnName == SSourceControlResolveWidgetDefs::ColumnID_IconLabel)
		{
			ItemContentWidget = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush(Item->GetIconName()))
					.ToolTipText(Item->GetIconTooltip())
				];
		}
		else if(ColumnName == SSourceControlResolveWidgetDefs::ColumnID_FileLabel)
		{
			ItemContentWidget = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(RowPadding)
				[
					SNew(STextBlock)
					.Text(Item->GetDisplayName())
				];
		}

		return ItemContentWidget.ToSharedRef();
	}

	// Packages dialog no longer valid; return a valid, null widget.
	return SNullWidget::NullWidget;
}


void SSourceControlResolveWidget::Construct(const FArguments& InArgs)
{
	ParentFrame = InArgs._ParentWindow.Get();
	SortByColumn = SSourceControlResolveWidgetDefs::ColumnID_FileLabel;
	SortMode = EColumnSortMode::Ascending;

	for (const auto& Item : InArgs._Items.Get())
	{
		ListViewItems.Add(MakeShared<FResolveItem>(Item));
	}

	TSharedRef<SHeaderRow> HeaderRowWidget = SNew(SHeaderRow);

	HeaderRowWidget->AddColumn(
		SHeaderRow::Column(SSourceControlResolveWidgetDefs::ColumnID_CheckBoxYoursLabel)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked(this, &SSourceControlResolveWidget::IsAllYoursChecked)
				.OnCheckStateChanged(this, &SSourceControlResolveWidget::CheckAllYours)
				.ToolTipText(LOCTEXT("ColumnCheckBoxYoursTooltip", "Resolve All Files Using Yours"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Subversion.CheckedOut_Small"))
				.ToolTipText(LOCTEXT("ColumnCheckBoxYoursTooltip", "Resolve All Files Using Yours"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ColumnCheckBoxYoursLabel", "Yours"))
				.ToolTipText(LOCTEXT("ColumnCheckBoxYoursTooltip", "Resolve All Files Using Yours"))
			]
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
		]
		.FixedWidth(80)
	);

	HeaderRowWidget->AddColumn(
		SHeaderRow::Column(SSourceControlResolveWidgetDefs::ColumnID_CheckBoxTheirsLabel)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked(this, &SSourceControlResolveWidget::IsAllTheirsChecked)
				.OnCheckStateChanged(this, &SSourceControlResolveWidget::CheckAllTheirs)
				.ToolTipText(LOCTEXT("ColumnCheckBoxTheirsTooltip", "Resolve All Files Using Theirs"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("SourceControl.Actions.Sync"))
				.ToolTipText(LOCTEXT("ColumnCheckBoxTheirsTooltip", "Resolve All Files Using Theirs"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ColumnCheckBoxTheirsLabel", "Theirs"))
				.ToolTipText(LOCTEXT("ColumnCheckBoxTheirsTooltip", "Resolve All Files Using Theirs"))
			]
		]
		.FixedWidth(80)
	);

	HeaderRowWidget->AddColumn(
		SHeaderRow::Column(SSourceControlResolveWidgetDefs::ColumnID_IconLabel)
		[
			SNew(SSpacer)
		]
		.SortMode(this, &SSourceControlResolveWidget::GetColumnSortMode, SSourceControlResolveWidgetDefs::ColumnID_IconLabel)
		.FixedWidth(SSourceControlResolveWidgetDefs::IconColumnWidth)
	);

	HeaderRowWidget->AddColumn(
		SHeaderRow::Column(SSourceControlResolveWidgetDefs::ColumnID_FileLabel)
		.DefaultLabel(LOCTEXT("FileColumnLabel", "File"))
		.SortMode(this, &SSourceControlResolveWidget::GetColumnSortMode, SSourceControlResolveWidgetDefs::ColumnID_FileLabel)
		.OnSort(this, &SSourceControlResolveWidget::OnColumnSortModeChanged)
		.FillWidth(7.0f)
	);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.Padding(5)
			//.Padding(FMargin(5, 0))
			[
				SNew(SBorder)
				[
					SAssignNew(ListView, SListView<TSharedPtr<FResolveItem>>)
					.ItemHeight(20)
					.ListItemsSource(&ListViewItems)
					.OnGenerateRow(this, &SSourceControlResolveWidget::OnGenerateRowForList)
					.HeaderRow(HeaderRowWidget)
					.SelectionMode(ESelectionMode::None)
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(5, 0, 5, 5))
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
				.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
				.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
				+SUniformGridPanel::Slot(0,0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.IsEnabled(this, &SSourceControlResolveWidget::IsOKEnabled)
					.Text( NSLOCTEXT("SourceControl.ResolvePanel", "OKButton", "OK") )
					.OnClicked(this, &SSourceControlResolveWidget::OKClicked)
				]
				+SUniformGridPanel::Slot(1,0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.Text( NSLOCTEXT("SourceControl.ResolvePanel", "CancelButton", "Cancel") )
					.OnClicked(this, &SSourceControlResolveWidget::CancelClicked)
				]
			]
		]
	];

	RequestSort();

	DialogResult = EResolveResults::RESOLVE_CANCELED;
}

FReply SSourceControlResolveWidget::OKClicked()
{
	DialogResult = EResolveResults::RESOLVE_ACCEPTED;
	ParentFrame.Pin()->RequestDestroyWindow();

	return FReply::Handled();
}


FReply SSourceControlResolveWidget::CancelClicked()
{
	DialogResult = EResolveResults::RESOLVE_CANCELED;
	ParentFrame.Pin()->RequestDestroyWindow();

	return FReply::Handled();
}


bool SSourceControlResolveWidget::IsOKEnabled() const
{
	for(const auto &Item : ListViewItems)
	{
		if(Item->GetResolveOption() != FResolveItem::None)
			return true;
	}
	
	return false;
}

TSharedRef<ITableRow> SSourceControlResolveWidget::OnGenerateRowForList(TSharedPtr<FResolveItem> ResolveItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<ITableRow> Row =
		SNew(SSourceControlResolveListRow, OwnerTable)
		.SourceControlResolveWidget(SharedThis(this))
		.Item(ResolveItem);

	return Row;
}


EColumnSortMode::Type SSourceControlResolveWidget::GetColumnSortMode(const FName ColumnId) const
{
	if (SortByColumn != ColumnId)
	{
		return EColumnSortMode::None;
	}

	return SortMode;
}


void SSourceControlResolveWidget::OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode)
{
	SortByColumn = ColumnId;
	SortMode = InSortMode;

	RequestSort();
}


void SSourceControlResolveWidget::RequestSort()
{
	// Sort the list of root items
	SortTree();

	ListView->RequestListRefresh();
}


void SSourceControlResolveWidget::SortTree()
{
	if (SortByColumn == SSourceControlResolveWidgetDefs::ColumnID_FileLabel)
	{
		if (SortMode == EColumnSortMode::Ascending)
		{
			ListViewItems.Sort([](const TSharedPtr<FResolveItem>& A, const TSharedPtr<FResolveItem>& B) {
				return A->GetDisplayName().ToString() < B->GetDisplayName().ToString(); });
		}
		else if (SortMode == EColumnSortMode::Descending)
		{
			ListViewItems.Sort([](const TSharedPtr<FResolveItem>& A, const TSharedPtr<FResolveItem>& B) {
				return A->GetDisplayName().ToString() >= B->GetDisplayName().ToString(); });
		}
	}
}

ECheckBoxState SSourceControlResolveWidget::IsAllYoursChecked() const
{
	for(const auto& Item : ListViewItems)
	{
		if(Item->GetResolveOption() != FResolveItem::Yours)
			return ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Checked;
}

ECheckBoxState SSourceControlResolveWidget::IsAllTheirsChecked() const
{
	for(const auto& Item : ListViewItems)
	{
		if(Item->GetResolveOption() != FResolveItem::Theirs)
			return ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Checked;
}

void SSourceControlResolveWidget::CheckAllYours(const ECheckBoxState CheckBoxState)
{
	if(CheckBoxState == ECheckBoxState::Checked)
	{
		for(auto& Item : ListViewItems)
		{
			Item->SetResolveOption(FResolveItem::Yours);
		}
	}
	else
	{
		for(auto& Item : ListViewItems)
		{
			if(Item->GetResolveOption() == FResolveItem::Yours)
				Item->SetResolveOption(FResolveItem::None);
		}
	}

	ListView->RequestListRefresh();
}

void SSourceControlResolveWidget::CheckAllTheirs(const ECheckBoxState CheckBoxState)
{
	if(CheckBoxState == ECheckBoxState::Checked)
	{
		for(auto& Item : ListViewItems)
		{
			Item->SetResolveOption(FResolveItem::Theirs);
		}
	}
	else
	{
		for(auto& Item : ListViewItems)
		{
			if(Item->GetResolveOption() == FResolveItem::Theirs)
				Item->SetResolveOption(FResolveItem::None);
		}
	}

	ListView->RequestListRefresh();
}

void SSourceControlResolveWidget::GetFilenamesForResolveYours(TArray<FString>& OutFilenames) const
{
	for(const auto& Item : ListViewItems)
	{
		if(Item->GetResolveOption() == FResolveItem::Yours)
		{
			OutFilenames.Add(Item->GetFilename());
		}
	}
}

void SSourceControlResolveWidget::GetFilenamesForResolveTheirs(TArray<FString>& OutFilenames) const
{
	for(const auto& Item : ListViewItems)
	{
		if(Item->GetResolveOption() == FResolveItem::Theirs)
		{
			OutFilenames.Add(Item->GetFilename());
		}
	}
}

#undef LOCTEXT_NAMESPACE

#endif // SOURCE_CONTROL_WITH_SLATE
