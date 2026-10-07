namespace Chimera.Client.Common
{
	public enum ESoundOutputMethod
	{
		LegacyDirectSound, // kept here to handle old configs
		XAudio2, // kept here to handle old configs; output removed, maps to OpenAL
		OpenAL,
		Dummy,
	}

	public enum EDispManagerAR
	{
		None = 0,
		System = 1,
		CustomSize = 2,
		CustomRatio = 3,
	}

	public enum SaveStateType
	{
		Binary,
		Text,
	}
}
