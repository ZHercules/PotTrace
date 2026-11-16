using Microsoft.Data.Sqlite;
namespace PotTrace.Storage;
public sealed class Database(string path)
{
    public string Path {get;}=path;
    internal SemaphoreSlim WriteGate {get;}=new(1,1);
    internal SqliteConnection Open()
    {
        var c=new SqliteConnection(new SqliteConnectionStringBuilder {DataSource=Path,ForeignKeys=true,DefaultTimeout=3}.ToString());
        c.Open();return c;
    }
    public static async Task<Database> CreateAsync(string path)
    {
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(path))!);
        var db=new Database(path);
        using var c=db.Open();using var command=c.CreateCommand();command.CommandText="PRAGMA journal_mode=WAL;";await command.ExecuteNonQueryAsync();
        using var resource=typeof(Database).Assembly.GetManifestResourceStream("PotTrace.Storage.Migrations.001_initial.sql")!;
        using var reader=new StreamReader(resource);command.CommandText=await reader.ReadToEndAsync();await command.ExecuteNonQueryAsync();return db;
    }
    public async Task<string> IntegrityCheckAsync()
    {using var c=Open();using var cmd=c.CreateCommand();cmd.CommandText="PRAGMA integrity_check;";return (string)(await cmd.ExecuteScalarAsync())!;}
    internal static SqliteCommand Command(SqliteConnection c,SqliteTransaction? tx,string sql,params (string Name,object? Value)[] args)
    {
        var cmd=c.CreateCommand();cmd.Transaction=tx;cmd.CommandText=sql;
        foreach(var (name,value) in args)cmd.Parameters.AddWithValue(name,value??DBNull.Value);
        return cmd;
    }
}
