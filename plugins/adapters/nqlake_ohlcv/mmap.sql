-- Input of the nqlake_ohlcv adapter:
--   nqlake mmap -y --sql-file plugins/adapters/nqlake_ohlcv/mmap.sql
-- META_ shares instrument_id 10451 with META but is another security, outside our universe.
-- SPCX before 2026-06-12 (1781222400000000000 ns) is a SPAC ETF; the stock listed under SPCX starts that day.
-- The adapter drops bars with invalid prices and bad prints, so they stay in the query.
-- Rows come in time order, then symbol order, so a cross sees every instrument of each hour.
SELECT *
FROM market.us_equity_ohlcv
WHERE symbol <> 'META_'
  AND NOT (symbol = 'SPCX' AND ts_event < 1781222400000000000)
ORDER BY ts_event, symbol
