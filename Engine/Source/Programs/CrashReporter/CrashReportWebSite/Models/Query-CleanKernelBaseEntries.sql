USE [CrashReport]

UPDATE Buggs 
SET [SummaryV2] = '' 
WHERE [SummaryV2] = 'KERNELBASE.dll'

UPDATE Buggs 
SET [SummaryV2] = '' 
WHERE [SummaryV2] = 'UE4Editor-UnrealEd.dll'

UPDATE Crashes 
SET [Summary] = '' 
WHERE [Summary] = 'KERNELBASE.dll'

UPDATE Crashes 
SET [Summary] = '' 
WHERE [Summary] = 'UE4Editor-UnrealEd.dll'

UPDATE Crashes 
SET [Summary] = ''
WHERE [Summary] = 'AssertLog='

UPDATE Crashes 
SET [Summary] = ''
WHERE [Summary] = 'Fatal error!' 

UPDATE Crashes 
SET [Description] = ''
WHERE [Description] = 'No comment provided' 



--OUTPUT $action, Inserted.*, Deleted.*