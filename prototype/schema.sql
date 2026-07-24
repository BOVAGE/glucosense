-- schema.sql
-- Run this SQL script in your Supabase SQL Editor to set up the normalized database with UUIDs and unique custom IDs.

-- Drop elements if they already exist (order is important due to foreign keys)
DROP VIEW IF EXISTS v_sessions_details;
DROP TABLE IF EXISTS ppg_sessions;
DROP TABLE IF EXISTS participants;

-- 1. Create the Participants Table
CREATE TABLE participants (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(), -- Database internal unique identifier
    participant_id VARCHAR(50) UNIQUE NOT NULL,    -- Research/human-readable custom ID (e.g. 'P001')
    age INT NOT NULL,
    gender VARCHAR(20) NOT NULL,
    weight NUMERIC(5,2) NOT NULL,                  -- Weight in kg
    height NUMERIC(5,2) NOT NULL,                  -- Height in cm
    years_on_t2dm INT NOT NULL,                    -- Years diagnosed with T2DM
    medication TEXT,                               -- Current medication (optional)
    created_at TIMESTAMPTZ DEFAULT now()
);

-- 2. Create the PPG Sessions Table
CREATE TABLE ppg_sessions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(), -- Session unique identifier
    participant_uuid UUID NOT NULL REFERENCES participants(id) ON DELETE CASCADE,
    session_kind VARCHAR(50) NOT NULL,             -- 'fasting', '1hr_post_prandial', '2hr_post_prandial'
    ppg_data JSONB,                                -- JSON array of integers containing raw IR samples
    bgl NUMERIC(5,2),                              -- Blood Glucose Level (mg/dL)
    status VARCHAR(20) DEFAULT 'pending',          -- 'pending', 'recording', 'uploaded', 'completed'
    created_at TIMESTAMPTZ DEFAULT now()
);

-- Create index on status for faster polling of active/pending sessions
CREATE INDEX idx_ppg_sessions_status ON ppg_sessions(status);

-- 3. Create the Joined Database View (flattens the data for the Web Table, ESP32, & Python CSV exporter)
CREATE VIEW v_sessions_details AS
SELECT 
    s.id AS id,                                    -- Alias session UUID to 'id' for ESP32 compatibility
    s.created_at,
    s.participant_uuid,
    p.participant_id AS participant_id,            -- Expose the custom human-readable ID
    p.age,
    p.gender,
    p.weight AS weight_kg,
    p.height AS height_cm,
    p.years_on_t2dm,
    p.medication,                                  -- Expose current medication
    s.session_kind,
    s.ppg_data,
    s.bgl,
    s.status
FROM ppg_sessions s
JOIN participants p ON s.participant_uuid = p.id;

-- 4. Enable Row Level Security (RLS)
ALTER TABLE participants ENABLE ROW LEVEL SECURITY;
ALTER TABLE ppg_sessions ENABLE ROW LEVEL SECURITY;

-- Create open public access policies for ease of local testing
CREATE POLICY "Allow public read access" ON participants FOR SELECT USING (true);
CREATE POLICY "Allow public insert access" ON participants FOR INSERT WITH CHECK (true);
CREATE POLICY "Allow public update access" ON participants FOR UPDATE USING (true);
CREATE POLICY "Allow public delete access" ON participants FOR DELETE USING (true);

CREATE POLICY "Allow public read access" ON ppg_sessions FOR SELECT USING (true);
CREATE POLICY "Allow public insert access" ON ppg_sessions FOR INSERT WITH CHECK (true);
CREATE POLICY "Allow public update access" ON ppg_sessions FOR UPDATE USING (true);
CREATE POLICY "Allow public delete access" ON ppg_sessions FOR DELETE USING (true);

-- 5. Enable Realtime for the table so the web dashboard receives instant updates when states change
alter publication supabase_realtime add table ppg_sessions;
alter publication supabase_realtime add table participants;
