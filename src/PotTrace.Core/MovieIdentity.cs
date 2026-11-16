using System.Security.Cryptography;
using System.Text;
namespace PotTrace.Core;
public static class MovieIdentity
{
    public static string GetKey(string path)=>Path.GetFileName(path.Replace('/', '\\')).ToUpperInvariant();
    public static Guid SourceId(string path)=>new(SHA256.HashData(Encoding.UTF8.GetBytes(path.ToUpperInvariant())).AsSpan(0,16));
}
