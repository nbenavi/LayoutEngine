SELECT [Id]
      ,[ComputerName]
      ,[BuildVersion]
      ,[UserName]
      ,[Branch]
--    ,[UserNameId]
      ,[EpicAccountId]
FROM [CrashReport].[dbo].[Crashes]
--where [TimeOfCrash] > CAST(GETDATE() AS DATE)
where username != 'anonymous' and buildversion = '4.5.0.0' and branch = 'UE4-Releases/4.5'
group by ComputerName