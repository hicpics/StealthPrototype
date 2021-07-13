// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#pragma once

#include "ISourceControlLabel.h"

/** 
 * Abstraction of a 'Git tag'.
 */
class FGitSourceControlLabel : public ISourceControlLabel, public TSharedFromThis<FGitSourceControlLabel>
{
public:

	FGitSourceControlLabel( const FString& InName, int32 InRevision )
		: Name(InName)
		, Revision(InRevision)
	{
	}

	/** ISourceControlLabel implementation */
	virtual const FString& GetName() const override;
	virtual bool GetFileRevisions( const TArray<FString>& InFiles, TArray< TSharedRef<ISourceControlRevision, ESPMode::ThreadSafe> >& OutRevisions ) const override;
	virtual bool Sync( const TArray<FString>& InFilenames ) const override;

private:

	/** Label name */
	FString Name;

	/** Repository revision this label was created at */
	int32 Revision;
};