using System;
using System.Text.Json.Serialization;

namespace unlockfps_nc.Model
{
    /// <summary>
    /// The subset of a GitHub release payload this app needs (GET /repos/{owner}/{repo}/releases/latest).
    /// </summary>
    public class VersionInfo
    {
        [JsonPropertyName("tag_name")]
        public string TagName { get; set; } = string.Empty;

        [JsonPropertyName("html_url")]
        public string HtmlUrl { get; set; } = string.Empty;

        /// <summary>
        /// Converts the tag (for example v3.6.0) to the packed number Program.Version uses (3600).
        /// Returns 0 when the tag cannot be parsed, which the caller treats as "no update".
        /// </summary>
        public int ToVersionNumber()
        {
            var tag = TagName.Trim().TrimStart('v', 'V');
            var parts = tag.Split('.');
            if (parts.Length < 2)
                return 0;

            if (!int.TryParse(parts[0], out var major) || !int.TryParse(parts[1], out var minor))
                return 0;

            var build = 0;
            if (parts.Length > 2)
                _ = int.TryParse(parts[2], out build);

            return major * 1000 + minor * 100 + build;
        }
    }
}
