﻿// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Net.NetworkInformation;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Threading;
using System.Linq;
using UnrealBuildTool;

/// <summary>
/// Helper command used for cooking.
/// </summary>
/// <remarks>
/// Command line parameters used by this command:
/// -clean
/// </remarks>
public partial class Project : CommandUtils
{

	#region Utilities

	private static readonly object SyncLock = new object();

	/// <returns>The path for the BuildPatchTool executable depending on host platform.</returns>
	private static string GetBuildPatchToolExecutable()
	{
		switch (UnrealBuildTool.BuildHostPlatform.Current.Platform)
		{
			case UnrealTargetPlatform.Win32:
				return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine/Binaries/Win32/BuildPatchTool.exe");
			case UnrealTargetPlatform.Win64:
				return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine/Binaries/Win64/BuildPatchTool.exe");
			case UnrealTargetPlatform.Mac:
				return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine/Binaries/Mac/BuildPatchTool");
			case UnrealTargetPlatform.Linux:
				return CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine/Binaries/Linux/BuildPatchTool");
		}
		throw new AutomationException(string.Format("Unknown host platform for BuildPatchTool - {0}", UnrealBuildTool.BuildHostPlatform.Current.Platform));
	}

	/// <summary>
	/// Checks the existence of the BuildPatchTool executable exists and builds it if it is missing
	/// </summary>
	private static void EnsureBuildPatchToolExists()
	{
		string BuildPatchToolExe = GetBuildPatchToolExecutable();
		if (!CommandUtils.FileExists_NoExceptions(BuildPatchToolExe))
		{
			lock (SyncLock)
			{
				if (!CommandUtils.FileExists_NoExceptions(BuildPatchToolExe))
				{
					UE4BuildUtils.BuildBuildPatchTool(null, UnrealBuildTool.BuildHostPlatform.Current.Platform);
				}
			}
		}
	}

	/// <summary>
	/// Writes a pak response file to disk
	/// </summary>
	/// <param name="Filename"></param>
	/// <param name="ResponseFile"></param>
	private static void WritePakResponseFile(string Filename, Dictionary<string, string> ResponseFile, bool Compressed)
	{
		using (var Writer = new StreamWriter(Filename, false, new System.Text.UTF8Encoding(true)))
		{
			foreach (var Entry in ResponseFile)
			{
				string Line = String.Format("\"{0}\" \"{1}\"", Entry.Key, Entry.Value);
				if (Compressed)
				{
					Line += " -compress";
				}
				Writer.WriteLine(Line);
			}
		}
	}

	/// <summary>
	/// Loads streaming install chunk manifest file from disk
	/// </summary>
	/// <param name="Filename"></param>
	/// <returns></returns>
	private static HashSet<string> ReadPakChunkManifest(string Filename)
	{
		var ResponseFile = ReadAllLines(Filename);
		var Result = new HashSet<string>(ResponseFile, StringComparer.InvariantCultureIgnoreCase);
		return Result;
	}

    static public void RunUnrealPak(Dictionary<string, string> UnrealPakResponseFile, string OutputLocation, string EncryptionKeys, string PakOrderFileLocation, string PlatformOptions, bool Compressed, String PatchSourceContentPath)
	{
		if (UnrealPakResponseFile.Count < 1)
		{
			return;
		}
		string PakName = Path.GetFileNameWithoutExtension(OutputLocation);
		string UnrealPakResponseFileName = CombinePaths(CmdEnv.LogFolder, "PakList_" + PakName + ".txt");
		WritePakResponseFile(UnrealPakResponseFileName, UnrealPakResponseFile, Compressed);

		var UnrealPakExe = CombinePaths(CmdEnv.LocalRoot, "Engine/Binaries/Win64/UnrealPak.exe");

		Log("Running UnrealPak *******");
		string CmdLine = CommandUtils.MakePathSafeToUseWithCommandLine(OutputLocation) + " -create=" + CommandUtils.MakePathSafeToUseWithCommandLine(UnrealPakResponseFileName);
		if (!String.IsNullOrEmpty(EncryptionKeys))
		{
			CmdLine += " -sign=" + CommandUtils.MakePathSafeToUseWithCommandLine(EncryptionKeys);
		}
		if (GlobalCommandLine.Installed)
		{
			CmdLine += " -installed";
		}
		CmdLine += " -order=" + CommandUtils.MakePathSafeToUseWithCommandLine(PakOrderFileLocation);
		if (GlobalCommandLine.UTF8Output)
		{
			CmdLine += " -UTF8Output";
		}
        if (!String.IsNullOrEmpty(PatchSourceContentPath))
        {
            CmdLine += " -generatepatch=" + CommandUtils.MakePathSafeToUseWithCommandLine(PatchSourceContentPath);
        }
		CmdLine += PlatformOptions;
		RunAndLog(CmdEnv, UnrealPakExe, CmdLine, Options: ERunOptions.Default | ERunOptions.UTF8Output);
		Log("UnrealPak Done *******");
	}

	static public void LogDeploymentContext(DeploymentContext SC)
	{
		Log("Deployment Context **************");
		Log("ArchiveDirectory = {0}", SC.ArchiveDirectory);
		Log("RawProjectPath = {0}", SC.RawProjectPath);
		Log("IsCodeBasedUprojectFile = {0}", SC.IsCodeBasedProject);
		Log("DedicatedServer = {0}", SC.DedicatedServer);
		Log("Stage = {0}", SC.Stage);
		Log("StageTargetPlatform = {0}", SC.StageTargetPlatform.PlatformType.ToString());
		Log("LocalRoot = {0}", SC.LocalRoot);
		Log("ProjectRoot = {0}", SC.ProjectRoot);
		Log("PlatformDir = {0}", SC.PlatformDir);
		Log("StageProjectRoot = {0}", SC.StageProjectRoot);
		Log("ShortProjectName = {0}", SC.ShortProjectName);
		Log("StageDirectory = {0}", SC.StageDirectory);
		Log("SourceRelativeProjectRoot = {0}", SC.SourceRelativeProjectRoot);
		Log("RelativeProjectRootForStage = {0}", SC.RelativeProjectRootForStage);
		Log("RelativeProjectRootForUnrealPak = {0}", SC.RelativeProjectRootForUnrealPak);
		Log("ProjectArgForCommandLines = {0}", SC.ProjectArgForCommandLines);
		Log("RuntimeRootDir = {0}", SC.RuntimeRootDir);
		Log("RuntimeProjectRootDir = {0}", SC.RuntimeProjectRootDir);
		Log("UProjectCommandLineArgInternalRoot = {0}", SC.UProjectCommandLineArgInternalRoot);
		Log("PakFileInternalRoot = {0}", SC.PakFileInternalRoot);
		Log("UnrealFileServerInternalRoot = {0}", SC.UnrealFileServerInternalRoot);
		Log("End Deployment Context **************");
	}

	public static Dictionary<string, string> ConvertToLower(Dictionary<string, string> Mapping)
	{
		var Result = new Dictionary<string, string>();
		foreach (var Pair in Mapping)
		{
			Result.Add(Pair.Key, Pair.Value.ToLowerInvariant());
		}
		return Result;
	}

	public static void MaybeConvertToLowerCase(ProjectParams Params, DeploymentContext SC)
	{
		var BuildPlatform = SC.StageTargetPlatform;

		if (BuildPlatform.DeployLowerCaseFilenames(false))
		{
			SC.NonUFSStagingFiles = ConvertToLower(SC.NonUFSStagingFiles);
			SC.NonUFSStagingFilesDebug = ConvertToLower(SC.NonUFSStagingFilesDebug);
		}
		if (Params.UsePak(SC.StageTargetPlatform) && BuildPlatform.DeployPakInternalLowerCaseFilenames())
		{
			SC.UFSStagingFiles = ConvertToLower(SC.UFSStagingFiles);
		}
		else if (!Params.UsePak(SC.StageTargetPlatform) && BuildPlatform.DeployLowerCaseFilenames(true))
		{
			SC.UFSStagingFiles = ConvertToLower(SC.UFSStagingFiles);
		}
	}

    private static int StageLocalizationDataForCulture(DeploymentContext SC, string CultureName, string SourceDirectory, string DestinationDirectory = null, bool bRemap = true)
    {
        int FilesAdded = 0;

        CultureName = CultureName.Replace('-', '_');

        string[] LocaleTags = CultureName.Replace('-', '_').Split('_');

        List<string> PotentialParentCultures = new List<string>();
        
        if (LocaleTags.Length > 0)
        {
            if (LocaleTags.Length > 1 && LocaleTags.Length > 2)
            {
                PotentialParentCultures.Add(string.Join("_", LocaleTags[0], LocaleTags[1], LocaleTags[2]));
            }
            if (LocaleTags.Length > 2)
            {
                PotentialParentCultures.Add(string.Join("_", LocaleTags[0], LocaleTags[2]));
            }
            if (LocaleTags.Length > 1)
            {
                PotentialParentCultures.Add(string.Join("_", LocaleTags[0], LocaleTags[1]));
            }
            PotentialParentCultures.Add(LocaleTags[0]);
        }

        string[] FoundDirectories = CommandUtils.FindDirectories(true, "*", false, new string[] { SourceDirectory });
        foreach (string FoundDirectory in FoundDirectories)
        {
            string DirectoryName = CommandUtils.GetLastDirectoryName(FoundDirectory);
            string CanonicalizedPotentialCulture = DirectoryName.Replace('-', '_');

            if (PotentialParentCultures.Contains(CanonicalizedPotentialCulture))
            {
                FilesAdded += SC.StageFiles(StagedFileType.UFS, CombinePaths(SourceDirectory, DirectoryName), "*.locres", true, null, DestinationDirectory != null ? CombinePaths(DestinationDirectory, DirectoryName) : null, true, bRemap);
            }
        }

        return FilesAdded;
    }

	public static void CreateStagingManifest(ProjectParams Params, DeploymentContext SC)
	{
		if (!Params.Stage)
		{
			return;
		}
		var ThisPlatform = SC.StageTargetPlatform;


        if (Params.HasDLCName)
        {
            string DLCName = Params.DLCName;

            // Making a plugin, grab the binaries too
            SC.StageFiles(StagedFileType.NonUFS, CombinePaths(SC.ProjectRoot, "Plugins", DLCName), "*.uplugin", true, null, null, true);
            SC.StageFiles(StagedFileType.NonUFS, CombinePaths(SC.ProjectRoot, "Plugins", DLCName, "Binaries"), "libUE4-*.so", true, null, null, true);
            SC.StageFiles(StagedFileType.NonUFS, CombinePaths(SC.ProjectRoot, "Plugins", DLCName, "Binaries"), "UE4-*.dll", true, null, null, true);
            SC.StageFiles(StagedFileType.NonUFS, CombinePaths(SC.ProjectRoot, "Plugins", DLCName, "Binaries"), "libUE4Server-*.so", true, null, null, true);
            SC.StageFiles(StagedFileType.NonUFS, CombinePaths(SC.ProjectRoot, "Plugins", DLCName, "Binaries"), "UE4Server-*.dll", true, null, null, true);
            
            // Put all of the cooked dir into the staged dir
            if (SC.DedicatedServer || Params.DLCIncludeEngineContent)
            {
                // Dedicated server cook doesn't save shaders so no Engine dir is created
                SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.ProjectRoot, "Plugins", DLCName, "Saved", "Cooked", SC.CookPlatform), "*", true, new[] { "AssetRegistry.bin" }, "", true, !Params.UsePak(SC.StageTargetPlatform));
            }
            else
            {
                SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.ProjectRoot, "Plugins", DLCName, "Saved", "Cooked", SC.CookPlatform), "*", true, new[] { "AssetRegistry.bin", CommandUtils.CombinePaths("Engine", "*") }, "", true, !Params.UsePak(SC.StageTargetPlatform));
            }

            return;
        }


		ThisPlatform.GetFilesToDeployOrStage(Params, SC);


		// Stage any extra runtime dependencies from the receipts
		foreach(BuildReceipt Receipt in SC.StageTargetReceipts)
		{
			SC.StageRuntimeDependenciesFromReceipt(Receipt);
		}

		// Get the build.properties file
		// this file needs to be treated as a UFS file for casing, but NonUFS for being put into the .pak file
		// @todo: Maybe there should be a new category - UFSNotForPak
		string BuildPropertiesPath = CombinePaths(SC.LocalRoot, "Engine/Build");
		if (SC.StageTargetPlatform.DeployLowerCaseFilenames(true))
		{
			BuildPropertiesPath = BuildPropertiesPath.ToLowerInvariant();
		}
		SC.StageFiles(StagedFileType.NonUFS, BuildPropertiesPath, "build.properties", false, null, null, true);

		// move the UE4Commandline.txt file to the root of the stage
		// this file needs to be treated as a UFS file for casing, but NonUFS for being put into the .pak file
		// @todo: Maybe there should be a new category - UFSNotForPak
		string CommandLineFile = "UE4CommandLine.txt";
		if (SC.StageTargetPlatform.DeployLowerCaseFilenames(true))
		{
			CommandLineFile = CommandLineFile.ToLowerInvariant();
		}
		SC.StageFiles(StagedFileType.NonUFS, GetIntermediateCommandlineDir(SC), CommandLineFile, false, null, "", true, false);

        ConfigCacheIni PlatformGameConfig = new ConfigCacheIni(SC.StageTargetPlatform.PlatformType, "Game", CommandUtils.GetDirectoryName(Params.RawProjectPath));
        var ProjectContentRoot = CombinePaths(SC.ProjectRoot, "Content");
        var StageContentRoot = CombinePaths(SC.RelativeProjectRootForStage, "Content");
        
        if (!Params.CookOnTheFly && !Params.SkipCookOnTheFly) // only stage the UFS files if we are not using cook on the fly
        {


            // Initialize internationalization preset.
            string InternationalizationPreset = null;

            // Use parameters if provided.
            if (string.IsNullOrEmpty(InternationalizationPreset))
            {
                InternationalizationPreset = Params.InternationalizationPreset;
            }

            // Use configuration if otherwise lacking an internationalization preset.
            if (string.IsNullOrEmpty(InternationalizationPreset))
            {
                if (PlatformGameConfig != null)
                {
                    PlatformGameConfig.GetString("/Script/UnrealEd.ProjectPackagingSettings", "InternationalizationPreset", out InternationalizationPreset);
                }
            }

            // Error if no preset has been provided.
            if (string.IsNullOrEmpty(InternationalizationPreset))
            {
                throw new AutomationException("No internationalization preset was specified for packaging. This will lead to fatal errors when launching. Specify preset via commandline (-I18NPreset=) or project packaging settings (InternationalizationPreset).");
            }

            // Initialize cultures to stage.
            List<string> CulturesToStage = null;

            // Use parameters if provided.
            if (Params.CulturesToCook != null && Params.CulturesToCook.Count > 0)
            {
                CulturesToStage = Params.CulturesToCook;
            }

            // Use configuration if otherwise lacking cultures to stage.
            if (CulturesToStage == null || CulturesToStage.Count == 0)
            {
                if (PlatformGameConfig != null)
                {
                    PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "CulturesToStage", out CulturesToStage);
                }
            }

            // Error if no cultures have been provided.
            if (CulturesToStage == null || CulturesToStage.Count == 0)
            {
                throw new AutomationException("No cultures were specified for cooking and packaging. This will lead to fatal errors when launching. Specify culture codes via commandline (-CookCultures=) or using project packaging settings (+CulturesToStage).");
            }

            // Stage ICU internationalization data from Engine.
            SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.LocalRoot, "Engine", "Content", "Internationalization", InternationalizationPreset), "*", true, null, CombinePaths("Engine", "Content", "Internationalization"), true, !Params.UsePak(SC.StageTargetPlatform));

            // Engine ufs (content)
            SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.LocalRoot, "Engine/Config"), "*", true, null, null, false, !Params.UsePak(SC.StageTargetPlatform)); // TODO: Exclude localization data generation config files.

            // Stat prefix/suffix files (used for FPSChart, etc...)
            //@TODO: Avoid packaging stat files in shipping builds (only 6 KB, but still more than zero)
            SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.LocalRoot, "Engine/Content/Stats"), "*", true, null, null, false, !Params.UsePak(SC.StageTargetPlatform));

            if (Params.bUsesSlate)
            {
                if (Params.bUsesSlateEditorStyle)
                {
                    SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.LocalRoot, "Engine/Content/Editor/Slate"), "*", true, null, null, false, !Params.UsePak(SC.StageTargetPlatform));
                }
                SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.LocalRoot, "Engine/Content/Slate"), "*", true, null, null, false, !Params.UsePak(SC.StageTargetPlatform));
                SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.ProjectRoot, "Content/Slate"), "*", true, new string[] { "*.uasset" }, CombinePaths(SC.RelativeProjectRootForStage, "Content/Slate"), true, !Params.UsePak(SC.StageTargetPlatform));
            }
            foreach (string Culture in CulturesToStage)
            {
                StageLocalizationDataForCulture(SC, Culture, CombinePaths(SC.LocalRoot, "Engine/Content/Localization/Engine"), null, !Params.UsePak(SC.StageTargetPlatform));
            }
            SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.LocalRoot, "Engine/Plugins"), "*.uplugin", true, null, null, true, !Params.UsePak(SC.StageTargetPlatform));

            // Game ufs (content)

            SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.ProjectRoot), "*.uproject", false, null, CombinePaths(SC.RelativeProjectRootForStage), true, !Params.UsePak(SC.StageTargetPlatform));
            SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.ProjectRoot, "Config"), "*", true, null, CombinePaths(SC.RelativeProjectRootForStage, "Config"), true, !Params.UsePak(SC.StageTargetPlatform)); // TODO: Exclude localization data generation config files.
            SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.ProjectRoot, "Plugins"), "*.uplugin", true, null, null, true, !Params.UsePak(SC.StageTargetPlatform));
            foreach (string Culture in CulturesToStage)
            {
                StageLocalizationDataForCulture(SC, Culture, CombinePaths(SC.ProjectRoot, "Content/Localization/Game"), CombinePaths(SC.RelativeProjectRootForStage, "Content/Localization/Game"), !Params.UsePak(SC.StageTargetPlatform));
            }

            // Stage any additional UFS and NonUFS paths specified in the project ini files; these dirs are relative to the game content directory
            if (PlatformGameConfig != null)
            {
                List<string> ExtraUFSDirs;
                if (PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "DirectoriesToAlwaysStageAsUFS", out ExtraUFSDirs))
                {
                    // Each string has the format '(Path="TheDirToStage")'
                    foreach (var PathStr in ExtraUFSDirs)
                    {
                        var PathParts = PathStr.Split('"');
                        if (PathParts.Length == 3)
                        {
                            var RelativePath = PathParts[1];
                            SC.StageFiles(StagedFileType.UFS, CombinePaths(ProjectContentRoot, RelativePath), "*", true, null, CombinePaths(StageContentRoot, RelativePath), true, !Params.UsePak(SC.StageTargetPlatform));
                        }
                        else if (PathParts.Length == 1)
                        {
                            var RelativePath = PathParts[0];
                            SC.StageFiles(StagedFileType.UFS, CombinePaths(ProjectContentRoot, RelativePath), "*", true, null, CombinePaths(StageContentRoot, RelativePath), true, !Params.UsePak(SC.StageTargetPlatform));
                        }
                    }
                }

                List<string> ExtraNonUFSDirs;
                if (PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "DirectoriesToAlwaysStageAsNonUFS", out ExtraNonUFSDirs))
                {
                    // Each string has the format '(Path="TheDirToStage")'
                    foreach (var PathStr in ExtraNonUFSDirs)
                    {
                        var PathParts = PathStr.Split('"');
                        if (PathParts.Length == 3)
                        {
                            var RelativePath = PathParts[1];
                            SC.StageFiles(StagedFileType.NonUFS, CombinePaths(ProjectContentRoot, RelativePath));
                        }
                        else if (PathParts.Length == 1)
                        {
                            var RelativePath = PathParts[0];
                            SC.StageFiles(StagedFileType.UFS, CombinePaths(ProjectContentRoot, RelativePath), "*", true, null, CombinePaths(StageContentRoot, RelativePath), true, !Params.UsePak(SC.StageTargetPlatform));
                        }
                    }
                }
            }

            StagedFileType StagedFileTypeForMovies = StagedFileType.NonUFS;
            if (Params.FileServer)
            {
                // UFS is required when using a file server
                StagedFileTypeForMovies = StagedFileType.UFS;
            }

            if (SC.StageTargetPlatform.StageMovies)
            {
                SC.StageFiles(StagedFileTypeForMovies, CombinePaths(SC.LocalRoot, "Engine/Content/Movies"), "*", true, new string[] { "*.uasset", "*.umap" }, CombinePaths(SC.RelativeProjectRootForStage, "Engine/Content/Movies"), true, !Params.UsePak(SC.StageTargetPlatform));
                SC.StageFiles(StagedFileTypeForMovies, CombinePaths(SC.ProjectRoot, "Content/Movies"), "*", true, new string[] { "*.uasset", "*.umap" }, CombinePaths(SC.RelativeProjectRootForStage, "Content/Movies"), true, !Params.UsePak(SC.StageTargetPlatform));
            }

            // eliminate the sand box
            SC.StageFiles(StagedFileType.UFS, CombinePaths(SC.ProjectRoot, "Saved", "Cooked", SC.CookPlatform), "*", true, new string[] { "*.json" }, "", true, !Params.UsePak(SC.StageTargetPlatform));

            // CrashReportClient is a standalone slate app that does not look in the generated pak file, so it needs the Content/Slate and Shaders/StandaloneRenderer folders Non-UFS
            // @todo Make CrashReportClient more portable so we don't have to do this
            if (SC.bStageCrashReporter && UnrealBuildTool.UnrealBuildTool.PlatformSupportsCrashReporter(SC.StageTargetPlatform.PlatformType) && !SC.DedicatedServer)
            {
                //If the .dat file needs to be staged as NonUFS for non-Windows/Linux hosts we need to change the casing as we do with the build properties file above.
                SC.StageFiles(StagedFileType.NonUFS, CombinePaths(SC.LocalRoot, "Engine/Content/Slate"));
                SC.StageFiles(StagedFileType.NonUFS, CombinePaths(SC.LocalRoot, "Engine/Shaders/StandaloneRenderer"));

                SC.StageFiles(StagedFileType.NonUFS, CombinePaths(SC.LocalRoot, "Engine", "Content", "Internationalization", InternationalizationPreset), "*", true, null, CombinePaths("Engine", "Content", "Internationalization"), false, true);

                // Linux platform stages ICU in GetFilesToDeployOrStage(), accounting for the actual architecture
                if (SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Win64 ||
                    SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Win32 ||
                    SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Mac)
                {
                    SC.StageFiles(StagedFileType.NonUFS, CombinePaths(SC.LocalRoot, "Engine/Binaries/ThirdParty/ICU"));
                }

                // SSL libraries are only available for Win64 builds.
                // @see FPerforceSourceControlProvider::LoadSSLLibraries
                if (SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Win64)
                {
                    SC.StageFiles(StagedFileType.NonUFS, CombinePaths(SC.LocalRoot, "Engine/Binaries/ThirdParty/OpenSSL"));
                }
            }
        }
        else
        {
            if (PlatformGameConfig != null)
            {
                List<string> ExtraNonUFSDirs;
                if (PlatformGameConfig.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "DirectoriesToAlwaysStageAsNonUFS", out ExtraNonUFSDirs))
                {
                    // Each string has the format '(Path="TheDirToStage")'
                    foreach (var PathStr in ExtraNonUFSDirs)
                    {
                        var PathParts = PathStr.Split('"');
                        if (PathParts.Length == 3)
                        {
                            var RelativePath = PathParts[1];
                            SC.StageFiles(StagedFileType.NonUFS, CombinePaths(ProjectContentRoot, RelativePath));
                        }
                        else if (PathParts.Length == 1)
                        {
                            var RelativePath = PathParts[0];
                            SC.StageFiles(StagedFileType.UFS, CombinePaths(ProjectContentRoot, RelativePath), "*", true, null, CombinePaths(StageContentRoot, RelativePath), true, !Params.UsePak(SC.StageTargetPlatform));
                        }
                    }
                }
            }
        }
	}

	public static void DumpTargetManifest(Dictionary<string, string> Mapping, string Filename, string StageDir, List<string> CRCFiles)
	{
        // const string Iso8601DateTimeFormat = "yyyy-MM-ddTHH:mm:ssZ"; // probably should work
		// const string Iso8601DateTimeFormat = "o"; // predefined universal Iso standard format (has too many millisecond spaces for our read code in FDateTime.ParseISO8601
        const string Iso8601DateTimeFormat = "yyyy'-'MM'-'dd'T'HH':'mm':'ss'.'fffZ";

        
        if (Mapping.Count > 0)
		{
			var Lines = new List<string>();
			foreach (var Pair in Mapping)
			{
                string TimeStamp = File.GetLastWriteTimeUtc(Pair.Key).ToString(Iso8601DateTimeFormat);
				if (CRCFiles.Contains(Pair.Value))
				{
					byte[] FileData = File.ReadAllBytes(StageDir + "/" + Pair.Value);
					TimeStamp = BitConverter.ToString(System.Security.Cryptography.MD5.Create().ComputeHash(FileData)).Replace("-", string.Empty);
				}
				string Dest = Pair.Value + "\t" + TimeStamp;

				Lines.Add(Dest);
			}
			WriteAllLines(Filename, Lines.ToArray());
		}
	}

	public static void CopyManifestFilesToStageDir(Dictionary<string, string> Mapping, string StageDir, string ManifestName, List<string> CRCFiles)
	{
		string ManifestPath = "";
		string ManifestFile = "";
		if (!String.IsNullOrEmpty(ManifestName))
		{
			ManifestFile = "Manifest_" + ManifestName + ".txt";
			ManifestPath = CombinePaths(StageDir, ManifestFile);
			DeleteFile(ManifestPath);
		}
		foreach (var Pair in Mapping)
		{
			string Src = Pair.Key;
			string Dest = CombinePaths(StageDir, Pair.Value);
			if (Src != Dest)  // special case for things created in the staging directory, like the pak file
			{
				CopyFileIncremental(Src, Dest, bFilterSpecialLinesFromIniFiles:true);
			}
		}
		if (!String.IsNullOrEmpty(ManifestPath) && Mapping.Count > 0)
		{
			DumpTargetManifest(Mapping, ManifestPath, StageDir, CRCFiles);
			if (!FileExists(ManifestPath))
			{
				throw new AutomationException("Failed to write manifest {0}", ManifestPath);
			}
			CopyFile(ManifestPath, CombinePaths(CmdEnv.LogFolder, ManifestFile));
		}
	}

	public static void DumpManifest(Dictionary<string, string> Mapping, string Filename)
	{
		if (Mapping.Count > 0)
		{
			var Lines = new List<string>();
			foreach (var Pair in Mapping)
			{
				string Src = Pair.Key;
				string Dest = Pair.Value;

				Lines.Add("\"" + Src + "\" \"" + Dest + "\"");
			}
			WriteAllLines(Filename, Lines.ToArray());
		}
	}

	public static void DumpManifest(DeploymentContext SC, string BaseFilename, bool DumpUFSFiles = true)
	{
		DumpManifest(SC.NonUFSStagingFiles, BaseFilename + "_NonUFSFiles.txt");
		if (DumpUFSFiles)
		{
			DumpManifest(SC.NonUFSStagingFilesDebug, BaseFilename + "_NonUFSFilesDebug.txt");
		}
		DumpManifest(SC.UFSStagingFiles, BaseFilename + "_UFSFiles.txt");
	}

	public static void CopyUsingStagingManifest(ProjectParams Params, DeploymentContext SC)
	{
		CopyManifestFilesToStageDir(SC.NonUFSStagingFiles, SC.StageDirectory, "NonUFSFiles", SC.StageTargetPlatform.GetFilesForCRCCheck());

		if (!Params.NoDebugInfo)
		{
			CopyManifestFilesToStageDir(SC.NonUFSStagingFilesDebug, SC.StageDirectory, "DebugFiles", SC.StageTargetPlatform.GetFilesForCRCCheck());
		}

        bool bStageUnrealFileSystemFiles = !Params.CookOnTheFly && !ShouldCreatePak(Params) && !Params.FileServer;
		if (bStageUnrealFileSystemFiles)
		{
			CopyManifestFilesToStageDir(SC.UFSStagingFiles, SC.StageDirectory, "UFSFiles", SC.StageTargetPlatform.GetFilesForCRCCheck());
		}
	}

	/// <summary>
	/// Creates a pak file using staging context (single manifest)
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	private static void CreatePakUsingStagingManifest(ProjectParams Params, DeploymentContext SC)
	{
		Log("Creating pak using staging manifest.");

		DumpManifest(SC, CombinePaths(CmdEnv.LogFolder, "PrePak" + (SC.DedicatedServer ? "_Server" : "")));

		var UnrealPakResponseFile = CreatePakResponseFileFromStagingManifest(SC);

		CreatePak(Params, SC, UnrealPakResponseFile, SC.ShortProjectName);
	}

	/// <summary>
	/// Creates a pak response file using stage context
	/// </summary>
	/// <param name="SC"></param>
	/// <returns></returns>
	private static Dictionary<string, string> CreatePakResponseFileFromStagingManifest(DeploymentContext SC)
	{
		var UnrealPakResponseFile = new Dictionary<string, string>(StringComparer.InvariantCultureIgnoreCase);
		foreach (var Pair in SC.UFSStagingFiles)
		{
			string Src = Pair.Key;
			string Dest = Pair.Value;

			Dest = CombinePaths(PathSeparator.Slash, SC.PakFileInternalRoot, Dest);

			// there can be files that only differ in case only, we don't support that in paks as paks are case-insensitive
			if (UnrealPakResponseFile.ContainsKey(Src))
			{
				throw new AutomationException("Staging manifest already contains {0} (or a file that differs in case only)", Src);
			}
			UnrealPakResponseFile.Add(Src, Dest);
		}
		return UnrealPakResponseFile;
	}


    private static string GetReleasePakFilePath(DeploymentContext SC, ProjectParams Params, string ReleaseVersion, string PakName)
    {
        string ReleaseVersionPath = CombinePaths(SC.ProjectRoot, "Releases", ReleaseVersion, SC.StageTargetPlatform.GetCookPlatform(Params.DedicatedServer, false, Params.CookFlavor), PakName);
        return ReleaseVersionPath;
    }


	/// <summary>
	/// Creates a pak file using response file.
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	/// <param name="UnrealPakResponseFile"></param>
	/// <param name="PakName"></param>
	private static void CreatePak(ProjectParams Params, DeploymentContext SC, Dictionary<string, string> UnrealPakResponseFile, string PakName)
	{
        string PostFix = "";
        if (Params.IsGeneratingPatch)
        {
            PostFix += "_P";
        }
		var OutputRelativeLocation = CombinePaths(SC.RelativeProjectRootForStage, "Content/Paks/", PakName + "-" + SC.FinalCookPlatform + PostFix + ".pak");
		if (SC.StageTargetPlatform.DeployLowerCaseFilenames(true))
		{
			OutputRelativeLocation = OutputRelativeLocation.ToLowerInvariant();
		}
		OutputRelativeLocation = SC.StageTargetPlatform.Remap(OutputRelativeLocation);
		var OutputLocation = CombinePaths(SC.RuntimeRootDir, OutputRelativeLocation);
		// Add input file to controll order of file within the pak
		var PakOrderFileLocationBase = CombinePaths(SC.ProjectRoot, "Build", SC.FinalCookPlatform, "FileOpenOrder");
		var PakOrderFileLocation = CombinePaths(PakOrderFileLocationBase, "GameOpenOrder.log");
		if (!FileExists_NoExceptions(PakOrderFileLocation))
		{
			// Use a fall back, it doesn't matter if this file exists or not. GameOpenOrder.log is preferred however
			PakOrderFileLocation = CombinePaths(PakOrderFileLocationBase, "EditorOpenOrder.log");
		}

		bool bCopiedExistingPak = false;

		if (SC.StageTargetPlatform != SC.CookSourcePlatform)
		{
			// Check to see if we have an existing pak file we can use

			var SourceOutputRelativeLocation = CombinePaths(SC.RelativeProjectRootForStage, "Content/Paks/", PakName + "-" + SC.CookPlatform + PostFix + ".pak");
			if (SC.CookSourcePlatform.DeployLowerCaseFilenames(true))
			{
				SourceOutputRelativeLocation = SourceOutputRelativeLocation.ToLowerInvariant();
			}
			SourceOutputRelativeLocation = SC.CookSourcePlatform.Remap(SourceOutputRelativeLocation);
			var SourceOutputLocation = CombinePaths(SC.CookSourceRuntimeRootDir, SourceOutputRelativeLocation);

			if (FileExists_NoExceptions(SourceOutputLocation))
			{
				InternalUtils.SafeCreateDirectory(Path.GetDirectoryName(OutputLocation), true);

				if (InternalUtils.SafeCopyFile(SourceOutputLocation, OutputLocation))
				{
					Log("Copying source pak from {0} to {1} instead of creating new pak", SourceOutputLocation, OutputLocation);
					bCopiedExistingPak = true;
				}
			}
			if (!bCopiedExistingPak)
			{
				Log("Failed to copy source pak from {0} to {1}, creating new pak", SourceOutputLocation, OutputLocation);
			}
		}

        string PatchSourceContentPath = null;
        if (Params.HasBasedOnReleaseVersion && Params.IsGeneratingPatch)
        {
            // don't include the post fix in this filename because we are looking for the source pak path
            string PakFilename = PakName + "-" + SC.FinalCookPlatform + ".pak";
            PatchSourceContentPath = GetReleasePakFilePath(SC, Params, Params.BasedOnReleaseVersion, PakFilename);
            

            /*PatchSourceContentPath = Params.BasedOnReleaseVersion;
            if (PatchSourceContentPath.EndsWith(PakName) == false)
            {
                string Temp = Path.GetDirectoryName(PatchSourceContentPath) + PakName;
                if ( File.Exists(Temp))
                {
                    PatchSourceContentPath = Temp;
                }
            }*/
        }

		if (!bCopiedExistingPak)
		{
			RunUnrealPak(UnrealPakResponseFile, OutputLocation, Params.SignPak, PakOrderFileLocation, SC.StageTargetPlatform.GetPlatformPakCommandLine(), Params.Compressed, PatchSourceContentPath );
		}


        if (Params.HasCreateReleaseVersion)
        {
            // copy the created pak to the release version directory we might need this later if we want to generate patches
            //string ReleaseVersionPath = CombinePaths( SC.ProjectRoot, "Releases", Params.CreateReleaseVersion, SC.StageTargetPlatform.GetCookPlatform(Params.DedicatedServer, false, Params.CookFlavor), Path.GetFileName(OutputLocation) );
            string ReleaseVersionPath = GetReleasePakFilePath(SC, Params, Params.CreateReleaseVersion, Path.GetFileName(OutputLocation));

            File.Copy(OutputLocation, ReleaseVersionPath);
        }

		if (Params.CreateChunkInstall)
		{
			var RegEx = new Regex("pakchunk(\\d+)", RegexOptions.IgnoreCase | RegexOptions.Compiled);
			var Matches = RegEx.Matches(PakName);

			if (Matches.Count == 0 || Matches[0].Groups.Count < 2)
			{
				throw new AutomationException(String.Format("Failed Creating Chunk Install Data, Unable to parse chunk id from {0}", PakName));
			}

			int ChunkID = Convert.ToInt32(Matches[0].Groups[1].ToString());
			if (ChunkID != 0)
			{
				var BPTExe = GetBuildPatchToolExecutable();
				EnsureBuildPatchToolExists();

				string VersionString = Params.ChunkInstallVersionString;
				string ChunkInstallBasePath = CombinePaths(Params.ChunkInstallDirectory, SC.FinalCookPlatform);
				string RawDataPath = CombinePaths(ChunkInstallBasePath, VersionString, PakName);
				string RawDataPakPath = CombinePaths(RawDataPath, PakName + "-" + SC.FinalCookPlatform + PostFix + ".pak");
				//copy the pak chunk to the raw data folder
				if (InternalUtils.SafeFileExists(RawDataPakPath, true))
				{
					InternalUtils.SafeDeleteFile(RawDataPakPath, true);
				}
				InternalUtils.SafeCreateDirectory(RawDataPath, true);
				InternalUtils.SafeCopyFile(OutputLocation, RawDataPakPath);
				if (ChunkID != 0)
				{
					InternalUtils.SafeDeleteFile(OutputLocation, true);
				}
				if (Params.IsGeneratingPatch)
				{
					if (String.IsNullOrEmpty(PatchSourceContentPath))
					{
						throw new AutomationException(String.Format("Failed Creating Chunk Install Data. No source pak for patch pak {0} given", OutputLocation));
					}
					// If we are generating a patch, then we need to copy the original pak across
					// for distribution.
					string SourceRawDataPakPath = CombinePaths(RawDataPath, PakName + "-" + SC.FinalCookPlatform + ".pak");
					InternalUtils.SafeCopyFile(PatchSourceContentPath, SourceRawDataPakPath);
				}

				string BuildRoot = MakePathSafeToUseWithCommandLine(RawDataPath);
				string CloudDir = MakePathSafeToUseWithCommandLine(CombinePaths(ChunkInstallBasePath, "CloudDir"));
				string ManifestDir = CombinePaths(ChunkInstallBasePath, "ManifestDir");
				var AppID = 1; // For a chunk install this value doesn't seem to matter
				string AppName = String.Format("{0}_{1}", SC.ShortProjectName, PakName);
				string AppLaunch = ""; // For a chunk install this value doesn't seem to matter
				string ManifestFilename = AppName + VersionString + ".manifest";
				string SourceManifestPath = CombinePaths(CloudDir, ManifestFilename);
				string BackupManifestPath = CombinePaths(RawDataPath, ManifestFilename);
				string DestManifestPath = CombinePaths(ManifestDir, ManifestFilename);
				InternalUtils.SafeCreateDirectory(ManifestDir, true);

				string CmdLine = String.Format("-BuildRoot=\"{0}\" -CloudDir=\"{1}\" -AppID={2} -AppName=\"{3}\" -BuildVersion=\"{4}\" -AppLaunch=\"{5}\" -DataAgeThreshold=12", BuildRoot, CloudDir, AppID, AppName, VersionString, AppLaunch);
				CmdLine += " -AppArgs=\"\"";
				CmdLine += " -custom=\"bIsPatch=false\"";
				CmdLine += String.Format(" -customint=\"ChunkID={0}\"", ChunkID);
				CmdLine += " -customint=\"PakReadOrdering=0\"";
				CmdLine += " -stdout";

				RunAndLog(CmdEnv, BPTExe, CmdLine, Options: ERunOptions.Default | ERunOptions.UTF8Output);

				InternalUtils.SafeCopyFile(SourceManifestPath, BackupManifestPath);
				InternalUtils.SafeCopyFile(SourceManifestPath, DestManifestPath);
			}
			else
			{
				// add the first pak file as needing deployment and convert to lower case again if needed
				SC.UFSStagingFiles.Add(OutputLocation, OutputRelativeLocation);
			}
		}
		else
		{
			// add the pak file as needing deployment and convert to lower case again if needed
			SC.UFSStagingFiles.Add(OutputLocation, OutputRelativeLocation);
		}
	}

	/// <summary>
	/// Creates pak files using streaming install chunk manifests.
	/// </summary>
	/// <param name="Params"></param>
	/// <param name="SC"></param>
	private static void CreatePaksUsingChunkManifests(ProjectParams Params, DeploymentContext SC)
	{
		Log("Creating pak using streaming install manifests.");
		DumpManifest(SC, CombinePaths(CmdEnv.LogFolder, "PrePak" + (SC.DedicatedServer ? "_Server" : "")));

		var TmpPackagingPath = GetTmpPackagingPath(Params, SC);
		var ChunkListFilename = GetChunkPakManifestListFilename(Params, SC);
		var ChunkList = ReadAllLines(ChunkListFilename);
		var ChunkResponseFiles = new HashSet<string>[ChunkList.Length];
		for (int Index = 0; Index < ChunkList.Length; ++Index)
		{
			var ChunkManifestFilename = CombinePaths(TmpPackagingPath, ChunkList[Index]);
			ChunkResponseFiles[Index] = ReadPakChunkManifest(ChunkManifestFilename);
		}
		// We still want to have a list of all files to stage. We will use the chunk manifests
		// to put the files from staging manigest into the right chunk
		var StagingManifestResponseFile = CreatePakResponseFileFromStagingManifest(SC);
		// DefaultChunkIndex assumes 0 is the 'base' chunk
		const int DefaultChunkIndex = 0;
		var PakResponseFiles = new Dictionary<string, string>[ChunkList.Length];
		for (int Index = 0; Index < PakResponseFiles.Length; ++Index)
		{
			PakResponseFiles[Index] = new Dictionary<string, string>(StringComparer.InvariantCultureIgnoreCase);
		}
		foreach (var StagingFile in StagingManifestResponseFile)
		{
			bool bAddedToChunk = false;
			for (int ChunkIndex = 0; !bAddedToChunk && ChunkIndex < ChunkResponseFiles.Length; ++ChunkIndex)
			{
				if (ChunkResponseFiles[ChunkIndex].Contains(StagingFile.Key) || ChunkResponseFiles[ChunkIndex].Contains(StagingFile.Key.Replace('/','\\')))
				{
					PakResponseFiles[ChunkIndex].Add(StagingFile.Key, StagingFile.Value);
					bAddedToChunk = true;
				}
			}
			if (!bAddedToChunk)
			{
				PakResponseFiles[DefaultChunkIndex].Add(StagingFile.Key, StagingFile.Value);
			}
		}

		if (Params.CreateChunkInstall)
		{
			string ManifestDir = CombinePaths(Params.ChunkInstallDirectory, SC.FinalCookPlatform, "ManifestDir");
			if (InternalUtils.SafeDirectoryExists(ManifestDir))
			{
				foreach (string ManifestFile in Directory.GetFiles(ManifestDir, "*.manifest"))
				{
					InternalUtils.SafeDeleteFile(ManifestFile, true);
				}
			}
			string DestDir = CombinePaths(Params.ChunkInstallDirectory, SC.FinalCookPlatform, Params.ChunkInstallVersionString);
			if (InternalUtils.SafeDirectoryExists(DestDir))
			{
				InternalUtils.SafeDeleteDirectory(DestDir); 
			}
		}

		for (int ChunkIndex = 0; ChunkIndex < PakResponseFiles.Length; ++ChunkIndex)
		{
			var ChunkName = Path.GetFileNameWithoutExtension(ChunkList[ChunkIndex]);
			CreatePak(Params, SC, PakResponseFiles[ChunkIndex], ChunkName);
		}

		String ChunkLayerFilename = CombinePaths(GetTmpPackagingPath(Params, SC), GetChunkPakLayerListName());
		String OutputChunkLayerFilename = Path.Combine(SC.ProjectRoot, "Build", SC.FinalCookPlatform, "ChunkLayerInfo", GetChunkPakLayerListName());
		Directory.CreateDirectory(Path.GetDirectoryName(OutputChunkLayerFilename));
		File.Copy(ChunkLayerFilename, OutputChunkLayerFilename, true);
	}

	private static bool DoesChunkPakManifestExist(ProjectParams Params, DeploymentContext SC)
	{
		return FileExists_NoExceptions(GetChunkPakManifestListFilename(Params, SC));
	}

	private static string GetChunkPakManifestListFilename(ProjectParams Params, DeploymentContext SC)
	{
		return CombinePaths(GetTmpPackagingPath(Params, SC), "pakchunklist.txt");
	}

	private static string GetChunkPakLayerListName()
	{
		return "pakchunklayers.txt";
	}

	private static string GetTmpPackagingPath(ProjectParams Params, DeploymentContext SC)
	{
		return CombinePaths(Path.GetDirectoryName(Params.RawProjectPath), "Saved", "TmpPackaging", SC.StageTargetPlatform.GetCookPlatform(SC.DedicatedServer, false, Params.CookFlavor));
	}

	private static bool ShouldCreatePak(ProjectParams Params, DeploymentContext SC)
	{
        Platform.PakType Pak = SC.StageTargetPlatform.RequiresPak(Params);

        // we may care but we don't want. 
        if (Params.SkipPak)
            return false; 

        if (Pak == Platform.PakType.Always)
        {
            return true;
        }
        else if (Pak == Platform.PakType.Never)
        {
            return false;
        }
        else // DontCare
        {
            return (Params.Pak || Params.SignedPak || !String.IsNullOrEmpty(Params.SignPak));
        }
	}

	private static bool ShouldCreatePak(ProjectParams Params)
	{
        Platform.PakType Pak = Params.ClientTargetPlatformInstances[0].RequiresPak(Params);

        // we may care but we don't want. 
        if (Params.SkipPak)
            return false; 

        if (Pak == Platform.PakType.Always)
        {
            return true;
        }
        else if (Pak == Platform.PakType.Never)
        {
            return false;
        }
        else // DontCare
        {
            return (Params.Pak || Params.SignedPak || !String.IsNullOrEmpty(Params.SignPak));
        }
	}

	protected static void DeletePakFiles(string StagingDirectory)
	{
		var StagedFilesDir = new DirectoryInfo(StagingDirectory);
		StagedFilesDir.GetFiles("*.pak", SearchOption.AllDirectories).ToList().ForEach(File => File.Delete());
	}

	public static void ApplyStagingManifest(ProjectParams Params, DeploymentContext SC)
	{
		MaybeConvertToLowerCase(Params, SC);
		if (SC.Stage && !Params.NoCleanStage && !Params.SkipStage && !Params.IterativeDeploy)
		{
            try
            {
                DeleteDirectory(SC.StageDirectory);
            }
            catch (Exception Ex)
            {
                // Delete cooked data (if any) as it may be incomplete / corrupted.
                Log("Failed to delete staging directory "+SC.StageDirectory);
                AutomationTool.ErrorReporter.Error("Stage Failed.", (int)AutomationTool.ErrorCodes.Error_FailedToDeleteStagingDirectory);
                throw Ex;   
            }
		}
		else
		{
			try
			{
				// delete old pak files
				DeletePakFiles(SC.StageDirectory);
			}
            catch (Exception Ex)
            {
                // Delete cooked data (if any) as it may be incomplete / corrupted.
                Log("Failed to delete pak files in "+SC.StageDirectory);
                AutomationTool.ErrorReporter.Error("Stage Failed.", (int)AutomationTool.ErrorCodes.Error_FailedToDeleteStagingDirectory);
                throw Ex;   
            }
		}
		if (ShouldCreatePak(Params, SC))
		{
			if (Params.Manifests && DoesChunkPakManifestExist(Params, SC))
			{
				CreatePaksUsingChunkManifests(Params, SC);
			}
			else
			{
				CreatePakUsingStagingManifest(Params, SC);
			}
		}
		if (!SC.Stage || Params.SkipStage)
		{
			return;
		}
		DumpManifest(SC, CombinePaths(CmdEnv.LogFolder, "FinalCopy" + (SC.DedicatedServer ? "_Server" : "")), !Params.UsePak(SC.StageTargetPlatform));
		CopyUsingStagingManifest(Params, SC);

		var ThisPlatform = SC.StageTargetPlatform;
		ThisPlatform.PostStagingFileCopy(Params, SC);
	}

	private static string GetIntermediateCommandlineDir(DeploymentContext SC)
	{
		return CombinePaths(SC.ProjectRoot, "Intermediate/UAT", SC.FinalCookPlatform);
	}

	public static void WriteStageCommandline(string IntermediateCmdLineFile, ProjectParams Params, DeploymentContext SC)
	{
		// this file needs to be treated as a UFS file for casing, but NonUFS for being put into the .pak file. 
		// @todo: Maybe there should be a new category - UFSNotForPak
		if (SC.StageTargetPlatform.DeployLowerCaseFilenames(true))
		{
			IntermediateCmdLineFile = IntermediateCmdLineFile.ToLowerInvariant();
		}
		if (File.Exists(IntermediateCmdLineFile))
		{
			File.Delete(IntermediateCmdLineFile);
		}

		if(!SC.StageTargetPlatform.ShouldStageCommandLine(Params, SC))
		{
			return;
		}

		if (!string.IsNullOrEmpty(Params.StageCommandline) || !string.IsNullOrEmpty(Params.RunCommandline))
		{
			string FileHostParams = " ";
			if (Params.CookOnTheFly || Params.FileServer)
			{
				FileHostParams += "-filehostip=";
				bool FirstParam = true;
				if (UnrealBuildTool.BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
				{
					NetworkInterface[] Interfaces = NetworkInterface.GetAllNetworkInterfaces();
					foreach (NetworkInterface adapter in Interfaces)
					{
						if (adapter.NetworkInterfaceType != NetworkInterfaceType.Loopback)
						{
							IPInterfaceProperties IP = adapter.GetIPProperties();
							for (int Index = 0; Index < IP.UnicastAddresses.Count; ++Index)
							{
								if (IP.UnicastAddresses[Index].IsDnsEligible && IP.UnicastAddresses[Index].Address.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork)
								{
                                    if (!IsNullOrEmpty(Params.Port))
                                    {
                                        foreach (var Port in Params.Port)
                                        {
                                            if (!FirstParam)
                                            {
                                                FileHostParams += "+";
                                            }
                                            FirstParam = false;
                                            string[] PortProtocol = Port.Split(new char[] { ':' });
                                            if (PortProtocol.Length > 1)
                                            {
                                                FileHostParams += String.Format("{0}://{1}:{2}", PortProtocol[0], IP.UnicastAddresses[Index].Address.ToString(), PortProtocol[1]);
                                            }
                                            else
                                            {
                                                FileHostParams += IP.UnicastAddresses[Index].Address.ToString();
                                                FileHostParams += ":";
                                                FileHostParams += Params.Port;
                                            }

                                        }
                                    }
                                    else
                                    {
										if (!FirstParam)
										{
											FileHostParams += "+";
										}
										FirstParam = false;
										
										// use default port
                                        FileHostParams += IP.UnicastAddresses[Index].Address.ToString();
                                    }

								}
							}
						}
					}
				}
				else
				{
					NetworkInterface[] Interfaces = NetworkInterface.GetAllNetworkInterfaces();
					foreach (NetworkInterface adapter in Interfaces)
					{
						if (adapter.OperationalStatus == OperationalStatus.Up)
						{
							IPInterfaceProperties IP = adapter.GetIPProperties();
							for (int Index = 0; Index < IP.UnicastAddresses.Count; ++Index)
							{
								if (IP.UnicastAddresses[Index].IsDnsEligible)
								{
									if (!IsNullOrEmpty(Params.Port))
									{
										foreach (var Port in Params.Port)
										{
											if (!FirstParam)
											{
												FileHostParams += "+";
											}
											FirstParam = false;
											string[] PortProtocol = Port.Split(new char[] { ':' });
											if (PortProtocol.Length > 1)
											{
												FileHostParams += String.Format("{0}://{1}:{2}", PortProtocol[0], IP.UnicastAddresses[Index].Address.ToString(), PortProtocol[1]);
											}
											else
											{
												FileHostParams += IP.UnicastAddresses[Index].Address.ToString();
												FileHostParams += ":";
												FileHostParams += Params.Port;
											}
										}
									}
                                    else
                                    {
										if (!FirstParam)
										{
											FileHostParams += "+";
										}
										FirstParam = false;
										
										// use default port
                                        FileHostParams += IP.UnicastAddresses[Index].Address.ToString();
                                    }

								}
							}
						}
					}
				}
				const string LocalHost = "127.0.0.1";
				if (!IsNullOrEmpty(Params.Port))
				{
					foreach (var Port in Params.Port)
					{
						if (!FirstParam)
						{
							FileHostParams += "+";
						}
						FirstParam = false;
						string[] PortProtocol = Port.Split(new char[] { ':' });
						if (PortProtocol.Length > 1)
						{
							FileHostParams += String.Format("{0}://{1}:{2}", PortProtocol[0], LocalHost, PortProtocol[1]);
						}
						else
						{
							FileHostParams += LocalHost;
							FileHostParams += ":";
							FileHostParams += Params.Port;
						}

					}
				}
                else
                {
					if (!FirstParam)
					{
						FileHostParams += "+";
					}
					FirstParam = false;
					
					// use default port
                    FileHostParams += LocalHost;
                }
				FileHostParams += " ";
			}

			String ProjectFile = String.Format("{0} ", SC.ProjectArgForCommandLines);
			if (SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Mac || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Win64 || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Win32 || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Linux)
			{
				ProjectFile = "";
			}
			Directory.CreateDirectory(GetIntermediateCommandlineDir(SC));
			string CommandLine = String.Format("{0} {1} {2} {3}\n", ProjectFile, Params.StageCommandline.Trim(new char[] { '\"' }), Params.RunCommandline.Trim(new char[] { '\"' }), FileHostParams).Trim();
			if (Params.IterativeDeploy)
			{
				CommandLine += " -iterative";
			}
			File.WriteAllText(IntermediateCmdLineFile, CommandLine);
		}
		else if (!Params.IsCodeBasedProject)
		{
			String ProjectFile = String.Format("{0} ", SC.ProjectArgForCommandLines);
			if (SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Mac || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Win64 || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Win32 || SC.StageTargetPlatform.PlatformType == UnrealTargetPlatform.Linux)
			{
				ProjectFile = "";
			}
			Directory.CreateDirectory(GetIntermediateCommandlineDir(SC));
			File.WriteAllText(IntermediateCmdLineFile, ProjectFile);
		}
	}

	private static void WriteStageCommandline(ProjectParams Params, DeploymentContext SC)
	{
		// always delete the existing commandline text file, so it doesn't reuse an old one
		string IntermediateCmdLineFile = CombinePaths(GetIntermediateCommandlineDir(SC), "UE4CommandLine.txt");
		WriteStageCommandline(IntermediateCmdLineFile, Params, SC);
	}

	private static Dictionary<string, string> ReadDeployedManifest(ProjectParams Params, DeploymentContext SC, List<string> ManifestList)
	{
		Dictionary<string, string> DeployedFiles = new Dictionary<string, string>();
		List<string> CRCFiles = SC.StageTargetPlatform.GetFilesForCRCCheck();

		// read the manifest
		bool bContinueSearch = true;
		foreach (string Manifest in ManifestList)
		{
			int FilesAdded = 0;
			if (bContinueSearch)
			{
				string Data = File.ReadAllText (Manifest);
				string[] Lines = Data.Split ('\n');
				foreach (string Line in Lines)
				{
					string[] Pair = Line.Split ('\t');
					if (Pair.Length > 1)
					{
						string Filename = Pair [0];
						string TimeStamp = Pair [1];
						FilesAdded++;
						if (DeployedFiles.ContainsKey (Filename))
						{
							if ((CRCFiles.Contains (Filename) && DeployedFiles [Filename] != TimeStamp) || (!CRCFiles.Contains (Filename) && DateTime.Parse (DeployedFiles [Filename]) > DateTime.Parse (TimeStamp)))
							{
								DeployedFiles [Filename] = TimeStamp;
							}
						}
						else
						{
							DeployedFiles.Add (Filename, TimeStamp);
						}
					}
				}
			}
			File.Delete(Manifest);

			if (FilesAdded == 0 && bContinueSearch)
			{
				// no files have been deployed at all to this guy, so remove all previously added files and exit the loop as this means we need to deploy everything
				DeployedFiles.Clear ();
				bContinueSearch = false;
			}
		}

		return DeployedFiles;
	}

	protected static Dictionary<string, string> ReadStagedManifest(ProjectParams Params, DeploymentContext SC, string Manifest)
	{
		Dictionary <string, string> StagedFiles = new Dictionary<string, string>();
		List<string> CRCFiles = SC.StageTargetPlatform.GetFilesForCRCCheck();

		// get the staged manifest from staged directory
		string ManifestFile = SC.StageDirectory + "/" + Manifest;
		if (File.Exists(ManifestFile))
		{
			string Data = File.ReadAllText(ManifestFile);
			string[] Lines = Data.Split('\n');
			foreach (string Line in Lines)
			{
				string[] Pair = Line.Split('\t');
				if (Pair.Length > 1)
				{
					string Filename = Pair[0];
					string TimeStamp = Pair[1];
					if (!StagedFiles.ContainsKey(Filename))
					{
						StagedFiles.Add(Filename, TimeStamp);
					}
					else if ((CRCFiles.Contains(Filename) && StagedFiles[Filename] != TimeStamp) || (!CRCFiles.Contains(Filename) && DateTime.Parse(StagedFiles[Filename]) > DateTime.Parse(TimeStamp)))
					{
						StagedFiles[Filename] = TimeStamp;
					}
				}
			}
		}
		return StagedFiles;
	}

	protected static void WriteDeltaManifest(ProjectParams Params, DeploymentContext SC, Dictionary<string, string> DeployedFiles, Dictionary<string, string> StagedFiles, string DeltaManifest)
	{
		List<string> CRCFiles = SC.StageTargetPlatform.GetFilesForCRCCheck();
		List<string> DeltaFiles = new List<string>();
		foreach (KeyValuePair<string, string> File in StagedFiles)
		{
			bool bNeedsDeploy = true;
			if (DeployedFiles.ContainsKey(File.Key))
			{
				if (CRCFiles.Contains(File.Key))
				{
					bNeedsDeploy = (File.Value != DeployedFiles[File.Key]);
				}
				else
				{
					DateTime Staged = DateTime.Parse(File.Value);
					DateTime Deployed = DateTime.Parse(DeployedFiles[File.Key]);
					bNeedsDeploy = (Staged > Deployed);
				}
			}

			if (bNeedsDeploy)
			{
				DeltaFiles.Add(File.Key);
			}
		}

		// add the manifest
		if (!DeltaManifest.Contains("NonUFS"))
		{
			DeltaFiles.Add(DeploymentContext.NonUFSDeployedManifestFileName);
			DeltaFiles.Add(DeploymentContext.UFSDeployedManifestFileName);
		}

		// TODO: determine files which need to be removed

		// write out to the deltamanifest.json
		string ManifestFile = CombinePaths(SC.StageDirectory, DeltaManifest);
		StreamWriter Writer = System.IO.File.CreateText(ManifestFile);
		foreach (string Line in DeltaFiles)
		{
			Writer.WriteLine(Line);
		}
		Writer.Close();
	}

	#endregion

	#region Stage Command

	//@todo move this
	public static List<DeploymentContext> CreateDeploymentContext(ProjectParams Params, bool InDedicatedServer, bool DoCleanStage = false)
	{
		ParamList<string> ListToProcess = InDedicatedServer && (Params.Cook || Params.CookOnTheFly) ? Params.ServerCookedTargets : Params.ClientCookedTargets;
		var ConfigsToProcess = InDedicatedServer && (Params.Cook || Params.CookOnTheFly) ? Params.ServerConfigsToBuild : Params.ClientConfigsToBuild;

		List<UnrealTargetPlatform> PlatformsToStage = Params.ClientTargetPlatforms;
		if (InDedicatedServer && (Params.Cook || Params.CookOnTheFly))
		{
			PlatformsToStage = Params.ServerTargetPlatforms;
        }

        bool prefixArchiveDir = false;
        if (PlatformsToStage.Contains(UnrealTargetPlatform.Win32) && PlatformsToStage.Contains(UnrealTargetPlatform.Win64))
        {
            prefixArchiveDir = true;
        }

		List<DeploymentContext> DeploymentContexts = new List<DeploymentContext>();
		foreach (var StagePlatform in PlatformsToStage)
		{
			// Get the platform to get cooked data from, may differ from the stage platform
			UnrealTargetPlatform CookedDataPlatform = Params.GetCookedDataPlatformForClientTarget(StagePlatform);

			if (InDedicatedServer && (Params.Cook || Params.CookOnTheFly))
			{
				CookedDataPlatform = Params.GetCookedDataPlatformForServerTarget(StagePlatform);
			}

			List<string> ExecutablesToStage = new List<string>();

			string PlatformName = StagePlatform.ToString();
			string StageArchitecture = !String.IsNullOrEmpty(Params.SpecifiedArchitecture) ? Params.SpecifiedArchitecture : "";
			foreach (var Target in ListToProcess)
			{
				foreach (var Config in ConfigsToProcess)
				{
					string Exe = Target;
					if (Config != UnrealTargetConfiguration.Development)
					{
						Exe = Target + "-" + PlatformName + "-" + Config.ToString() + StageArchitecture;
					}
					ExecutablesToStage.Add(Exe);
				}
            }

            string StageDirectory = (Params.Stage || !String.IsNullOrEmpty(Params.StageDirectoryParam)) ? Params.BaseStageDirectory : "";
            string ArchiveDirectory = (Params.Archive || !String.IsNullOrEmpty(Params.ArchiveDirectoryParam)) ? Params.BaseArchiveDirectory : "";
            if (prefixArchiveDir && (StagePlatform == UnrealTargetPlatform.Win32 || StagePlatform == UnrealTargetPlatform.Win64))
            {
                if (Params.Stage)
                {
                    StageDirectory = CombinePaths(Params.BaseStageDirectory, StagePlatform.ToString());
                }
                if (Params.Archive)
                {
                    ArchiveDirectory = CombinePaths(Params.BaseArchiveDirectory, StagePlatform.ToString());
                }
            }

			string EngineDir = CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "Engine");

			List<BuildReceipt> TargetsToStage = new List<BuildReceipt>();
			foreach(string Target in ListToProcess)
			{
				foreach(UnrealTargetConfiguration Config in ConfigsToProcess)
				{
					string ReceiptBaseDir = Params.IsCodeBasedProject? Path.GetDirectoryName(Params.RawProjectPath) : EngineDir;

					Platform PlatformInstance = Platform.Platforms[StagePlatform];
					UnrealTargetPlatform[] SubPlatformsToStage = PlatformInstance.GetStagePlatforms();

					// if we are attempting to gathering multiple platforms, the files aren't required
					bool bRequireStagedFilesToExist = SubPlatformsToStage.Length == 1;

					foreach (UnrealTargetPlatform ReceiptPlatform in SubPlatformsToStage)
					{
						string Architecture = !String.IsNullOrEmpty(Params.SpecifiedArchitecture) ? Params.SpecifiedArchitecture : UEBuildPlatform.GetBuildPlatform(ReceiptPlatform).GetActiveArchitecture();
						string ReceiptFileName = BuildReceipt.GetDefaultPath(ReceiptBaseDir, Target, ReceiptPlatform, Config, Architecture);
						if(!File.Exists(ReceiptFileName))
						{
							if (bRequireStagedFilesToExist)
							{
								// if we aren't collecting multiple platforms, then it is expected to exist
								throw new AutomationException("Missing receipt '{0}'. Check that this target has been built.", Path.GetFileName(ReceiptFileName));
							}
							else
							{
								// if it's multiple platforms, then allow missing receipts
								continue;
							}

						}

						// Convert the paths to absolute
						BuildReceipt Receipt = BuildReceipt.Read(ReceiptFileName);
						Receipt.ExpandPathVariables(EngineDir, Path.GetDirectoryName(Params.RawProjectPath));
						Receipt.SetDependenciesToBeRequired(bRequireStagedFilesToExist);
						TargetsToStage.Add(Receipt);
					}
				}
			}

			//@todo should pull StageExecutables from somewhere else if not cooked
            var SC = new DeploymentContext(Params.RawProjectPath, CmdEnv.LocalRoot,
                StageDirectory,
                ArchiveDirectory,
				Params.CookFlavor,
				Params.GetTargetPlatformInstance(CookedDataPlatform),
				Params.GetTargetPlatformInstance(StagePlatform),
				ConfigsToProcess,
				TargetsToStage,
				ExecutablesToStage,
				InDedicatedServer,
				Params.Cook || Params.CookOnTheFly,
				Params.CrashReporter && !(StagePlatform == UnrealTargetPlatform.Linux && Params.Rocket), // can't include the crash reporter from binary Linux builds
				Params.Stage,
				Params.CookOnTheFly,
				Params.Archive,
				Params.IsProgramTarget,
				Params.HasDedicatedServerAndClient
				);
			LogDeploymentContext(SC);

			// If we're a derived platform make sure we're at the end, otherwise make sure we're at the front

			if (CookedDataPlatform != StagePlatform)
			{
				DeploymentContexts.Add(SC);
			}
			else
			{
				DeploymentContexts.Insert(0, SC);
			}
		}

		return DeploymentContexts;
	}

	public static void CopyBuildToStagingDirectory(ProjectParams Params)
	{
		if (ShouldCreatePak(Params) || (Params.Stage && !Params.SkipStage))
		{
			Params.ValidateAndLog();

			Log("********** STAGE COMMAND STARTED **********");

			if (!Params.NoClient)
			{
				var DeployContextList = CreateDeploymentContext(Params, false, true);
				foreach (var SC in DeployContextList)
				{
					// write out the commandline file now so it can go into the manifest
					WriteStageCommandline(Params, SC);
					CreateStagingManifest(Params, SC);
					ApplyStagingManifest(Params, SC);

					if (Params.IterativeDeploy)
					{
						// get the deployed file data
						Dictionary<string, string> DeployedUFSFiles = new Dictionary<string, string>();
						Dictionary<string, string> DeployedNonUFSFiles = new Dictionary<string, string>();
						List<string> UFSManifests;
						List<string> NonUFSManifests; 
						if (SC.StageTargetPlatform.RetrieveDeployedManifests(Params, SC, out UFSManifests, out NonUFSManifests))
						{
							DeployedUFSFiles = ReadDeployedManifest(Params, SC, UFSManifests);
							DeployedNonUFSFiles = ReadDeployedManifest(Params, SC, NonUFSManifests);
						}

						// get the staged file data
						Dictionary<string, string> StagedUFSFiles = ReadStagedManifest(Params, SC, DeploymentContext.UFSDeployedManifestFileName);
						Dictionary<string, string> StagedNonUFSFiles = ReadStagedManifest(Params, SC, DeploymentContext.NonUFSDeployedManifestFileName);

						// write out the delta file data
						WriteDeltaManifest(Params, SC, DeployedUFSFiles, StagedUFSFiles, DeploymentContext.UFSDeployDeltaFileName);
						WriteDeltaManifest(Params, SC, DeployedNonUFSFiles, StagedNonUFSFiles, DeploymentContext.NonUFSDeployDeltaFileName);
					}
				}
			}

			if (Params.DedicatedServer)
			{
				var DeployContextList = CreateDeploymentContext(Params, true, true);
				foreach (var SC in DeployContextList)
				{
					CreateStagingManifest(Params, SC);
					ApplyStagingManifest(Params, SC);
				}
			}
			Log("********** STAGE COMMAND COMPLETED **********");
		}
	}

	#endregion
}
