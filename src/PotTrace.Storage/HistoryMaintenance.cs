using PotTrace.Core;
namespace PotTrace.Storage;
public sealed class HistoryMaintenance
{
    private readonly Database database;
    private readonly IngestionWriter writer;
    public HistoryMaintenance(Database database,IngestionWriter writer) {this.database=database;this.writer=writer;}
    public async Task<int> DeleteAsync(HistoryFilter filter,CancellationToken ct)
    {
        await database.WriteGate.WaitAsync(ct);
        try
        {
            using var c=database.Open();using var tx=c.BeginTransaction();
            using var select=Database.Command(c,tx,$"SELECT s.Payload FROM Session s WHERE {HistoryRepository.Where}");HistoryRepository.Filter(select,filter);
            var sessions=await HistoryRepository.ReadJson<SessionRecord>(select,ct);
            foreach(var s in sessions)
            {
                using var tombstone=Database.Command(c,tx,"INSERT OR IGNORE INTO DeletionTombstone VALUES($instance,$epoch)",("$instance",s.InstanceId.ToString()),("$epoch",s.MediaEpoch.ToString()));await tombstone.ExecuteNonQueryAsync(ct);
                using var remove=Database.Command(c,tx,"DELETE FROM Session WHERE Id=$id",("$id",s.SessionId.ToString()));await remove.ExecuteNonQueryAsync(ct);
            }
            writer.DiscardSessions(sessions.Select(x=>x.SessionId).ToHashSet());writer.SaveCheckpoint(c,tx);tx.Commit();return sessions.Count;
        }
        finally{database.WriteGate.Release();}
    }
}
