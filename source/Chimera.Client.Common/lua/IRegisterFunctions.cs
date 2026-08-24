namespace Chimera.Client.Common
{
	public interface IRegisterFunctions
	{
		LuaLibraryBase.NLFAddCallback CreateAndRegisterNamedFunction { get; set; }
	}
}
