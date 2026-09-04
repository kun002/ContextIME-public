using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using System.Web.Script.Serialization;
using Microsoft.Win32;

namespace ContextIME.NativeTests
{
    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            SmokeOptions options;
            try
            {
                options = SmokeOptions.Parse(args);
            }
            catch (Exception exception)
            {
                File.WriteAllText(Path.Combine(Path.GetTempPath(), "contextime-tsf-smoke-startup-error.txt"), exception.ToString());
                return 2;
            }

            Trace(options, "parsed arguments");

            NativeMethods.SetProcessDPIAware();
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            SmokeResult result;
            using (SmokeForm form = new SmokeForm(options))
            {
                Trace(options, "entering application message loop");
                Application.Run(form);
                Trace(options, "application message loop exited");
                result = form.Result;
            }

            try
            {
                string parent = Path.GetDirectoryName(options.EvidencePath);
                if (!String.IsNullOrEmpty(parent))
                {
                    Directory.CreateDirectory(parent);
                }
                JavaScriptSerializer serializer = new JavaScriptSerializer();
                File.WriteAllText(options.EvidencePath, serializer.Serialize(result), new UTF8Encoding(false));
            }
            catch (Exception exception)
            {
                Trace(options, "evidence write failed: " + exception);
                return 3;
            }

            return result.Passed ? 0 : 1;
        }

        internal static void Trace(SmokeOptions options, string message)
        {
            try
            {
                File.AppendAllText(options.EvidencePath + ".trace.log", DateTime.UtcNow.ToString("o") + " " + message + Environment.NewLine, new UTF8Encoding(false));
            }
            catch
            {
            }
        }
    }

    internal sealed class SmokeOptions
    {
        public string EvidencePath;
        public string SequencePath = String.Empty;
        public SmokeSequence Sequence;
        public string InputText = "shurufa";
        public string ExpectedText = String.Empty;
        public string ExpectedEnglish = "abc";
        public string FollowupInputText = String.Empty;
        public string ExpectedFollowupText = String.Empty;
        public string SelectionKey = "space";
        public int PreSelectionBackspaceCount;
        public int PageDownCount;
        public int TimeoutMilliseconds = 8000;
        public Guid TextService = new Guid("9FA3541F-F3F9-4C67-AA42-6C9AB15FB6A9");
        public Guid Profile = new Guid("A200BA94-B1A7-4668-A22B-CA61EC1E79F7");
        public ushort Language = 0x0804;

        public static SmokeOptions Parse(string[] args)
        {
            SmokeOptions options = new SmokeOptions();
            for (int index = 0; index < args.Length; index++)
            {
                string name = args[index];
                if (index + 1 >= args.Length)
                {
                    throw new ArgumentException("Missing value for " + name);
                }
                string value = args[++index];
                if (name == "--evidence") options.EvidencePath = Path.GetFullPath(value);
                else if (name == "--sequence") options.SequencePath = Path.GetFullPath(value);
                else if (name == "--input") options.InputText = value;
                else if (name == "--expected") options.ExpectedText = value;
                else if (name == "--expected-english") options.ExpectedEnglish = value;
                else if (name == "--followup-input") options.FollowupInputText = value;
                else if (name == "--expected-followup") options.ExpectedFollowupText = value;
                else if (name == "--selection-key") options.SelectionKey = value.ToLowerInvariant();
                else if (name == "--pre-selection-backspace-count") options.PreSelectionBackspaceCount = Int32.Parse(value);
                else if (name == "--page-down-count") options.PageDownCount = Int32.Parse(value);
                else if (name == "--timeout-ms") options.TimeoutMilliseconds = Int32.Parse(value);
                else if (name == "--text-service") options.TextService = new Guid(value);
                else if (name == "--profile") options.Profile = new Guid(value);
                else if (name == "--language") options.Language = UInt16.Parse(value);
                else throw new ArgumentException("Unknown argument: " + name);
            }

            if (String.IsNullOrEmpty(options.EvidencePath)) throw new ArgumentException("--evidence is required.");
            if (String.IsNullOrEmpty(options.InputText)) throw new ArgumentException("--input cannot be empty.");
            if (String.IsNullOrEmpty(options.ExpectedEnglish)) throw new ArgumentException("--expected-english cannot be empty.");
            if (String.IsNullOrEmpty(options.FollowupInputText) != String.IsNullOrEmpty(options.ExpectedFollowupText))
            {
                throw new ArgumentException("--followup-input and --expected-followup must either both be set or both be empty.");
            }
            bool numericSelection = options.SelectionKey.Length == 1 && options.SelectionKey[0] >= '1' && options.SelectionKey[0] <= '9';
            bool namedSelection = String.Equals(options.SelectionKey, "space", StringComparison.Ordinal) ||
                String.Equals(options.SelectionKey, "enter", StringComparison.Ordinal) ||
                String.Equals(options.SelectionKey, "escape", StringComparison.Ordinal);
            if (!namedSelection && !numericSelection)
            {
                throw new ArgumentException("--selection-key must be space, enter, escape, or a digit from 1 through 9.");
            }
            if (options.PreSelectionBackspaceCount < 0 || options.PreSelectionBackspaceCount > 100) throw new ArgumentOutOfRangeException("--pre-selection-backspace-count");
            if (options.PageDownCount < 0 || options.PageDownCount > 10) throw new ArgumentOutOfRangeException("--page-down-count");
            if (options.TimeoutMilliseconds < 1000 || options.TimeoutMilliseconds > 60000) throw new ArgumentOutOfRangeException("--timeout-ms");
            if (!String.IsNullOrEmpty(options.SequencePath))
            {
                JavaScriptSerializer serializer = new JavaScriptSerializer();
                options.Sequence = serializer.Deserialize<SmokeSequence>(File.ReadAllText(options.SequencePath, Encoding.UTF8));
                if (options.Sequence == null || String.IsNullOrEmpty(options.Sequence.Name) ||
                    options.Sequence.Steps == null || options.Sequence.Steps.Length == 0)
                {
                    throw new ArgumentException("--sequence must contain a name and at least one step.");
                }
                for (int stepIndex = 0; stepIndex < options.Sequence.Steps.Length; stepIndex++)
                {
                    SmokeSequenceStep step = options.Sequence.Steps[stepIndex];
                    if (step == null || String.IsNullOrEmpty(step.Name) || String.IsNullOrEmpty(step.Input) || String.IsNullOrEmpty(step.ExpectedCommit))
                    {
                        throw new ArgumentException("Every sequence step must contain name, input and expectedCommit.");
                    }
                    string stepSelection = (step.SelectionKey ?? String.Empty).ToLowerInvariant();
                    bool stepNumeric = stepSelection.Length == 1 && stepSelection[0] >= '1' && stepSelection[0] <= '9';
                    if (!stepNumeric && !String.Equals(stepSelection, "space", StringComparison.Ordinal) &&
                        !String.Equals(stepSelection, "enter", StringComparison.Ordinal))
                    {
                        throw new ArgumentException("Sequence selectionKey must be space, enter, or a digit from 1 through 9.");
                    }
                    step.SelectionKey = stepSelection;
                }
            }
            return options;
        }
    }

    internal sealed class SmokeSequence
    {
        public string Name = String.Empty;
        public SmokeSequenceStep[] Steps = new SmokeSequenceStep[0];
    }

    internal sealed class SmokeSequenceStep
    {
        public string Name = String.Empty;
        public string Input = String.Empty;
        public string SelectionKey = String.Empty;
        public string ExpectedCommit = String.Empty;
    }

    internal sealed class SmokeForm : Form
    {
        private static readonly Guid InputProcessorProfilesClass = new Guid("33C53A50-F456-4884-B049-85FD643ECFED");
        private const int CandidateAppearancePixelThreshold = 1500;
        private const int CandidatePageChangePixelThreshold = 500;

        private readonly SmokeOptions options;
        private readonly TextBox input;
        private readonly Label status;
        private readonly System.Windows.Forms.Timer timer;
        private readonly Stopwatch phaseClock = new Stopwatch();
        private readonly Dictionary<long, WindowEvidence> candidateWindowPeaks = new Dictionary<long, WindowEvidence>();
        private readonly Dictionary<long, WindowEvidence> latestServerWindows = new Dictionary<long, WindowEvidence>();
        private Bitmap candidateBaseline;
        private Bitmap candidateScreenshot;
        private Bitmap firstPageScreenshot;
        private Bitmap pagedCandidateScreenshot;
        private Rectangle candidateCaptureRegion;
        private int maximumCandidateChangedPixels;
        private int maximumPageChangedPixels;
        private ITfInputProcessorProfiles profiles;
        private int phase;
        private int profileActivationHResult = unchecked((int)0x80004005);
        private string committedText = String.Empty;
        private int lastTracedPhase = -1;
        private bool foregroundAcquired;
        private bool capsLockStateCaptured;
        private int sequenceStepIndex;
        private string sequenceTextBeforeStep = String.Empty;
        private string sequenceExpectedText = String.Empty;

        public SmokeResult Result { get; private set; }

        public SmokeForm(SmokeOptions options)
        {
            this.options = options;
            Result = SmokeResult.Create(options);

            Text = "ContextIME real TSF smoke test";
            ClientSize = new Size(680, 520);
            StartPosition = FormStartPosition.CenterScreen;
            TopMost = true;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;

            input = new TextBox();
            input.Font = new Font("Microsoft YaHei UI", 20F);
            input.Location = new Point(16, 52);
            input.Size = new Size(648, 43);
            Controls.Add(input);

            status = new Label();
            status.AutoSize = true;
            status.Location = new Point(16, 18);
            status.Text = "Starting real TSF smoke test...";
            Controls.Add(status);

            timer = new System.Windows.Forms.Timer();
            timer.Interval = 75;
            timer.Tick += OnTick;
            Shown += OnShown;
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                RestoreCapsLockState();
                timer.Dispose();
                if (candidateBaseline != null) candidateBaseline.Dispose();
                if (candidateScreenshot != null) candidateScreenshot.Dispose();
                if (firstPageScreenshot != null) firstPageScreenshot.Dispose();
                if (pagedCandidateScreenshot != null) pagedCandidateScreenshot.Dispose();
                if (profiles != null && Marshal.IsComObject(profiles)) Marshal.FinalReleaseComObject(profiles);
                input.Dispose();
                status.Dispose();
            }
            base.Dispose(disposing);
        }

        private void OnShown(object sender, EventArgs eventArgs)
        {
            Program.Trace(options, "form shown");
            input.Focus();
            foregroundAcquired = AcquireForeground();
            Result.ForegroundAcquired = foregroundAcquired;
            NormalizeCapsLockState();
            if (!Result.CapsLockNormalized)
            {
                Fail("Caps Lock could not be normalized before typing the deterministic ASCII fixture.");
                return;
            }
            try
            {
                Program.Trace(options, "creating input processor profiles COM object");
                Type type = Type.GetTypeFromCLSID(InputProcessorProfilesClass, true);
                profiles = (ITfInputProcessorProfiles)Activator.CreateInstance(type);
                Guid service = options.TextService;
                Guid profile = options.Profile;
                Program.Trace(options, "calling ActivateLanguageProfile");
                profileActivationHResult = profiles.ActivateLanguageProfile(ref service, options.Language, ref profile);
                Program.Trace(options, "ActivateLanguageProfile returned " + HResult(profileActivationHResult));

                ushort activeLanguage;
                Guid activeProfile;
                int activeResult = profiles.GetActiveLanguageProfile(ref service, out activeLanguage, out activeProfile);
                Program.Trace(options, "GetActiveLanguageProfile returned " + HResult(activeResult));
                Result.ProfileActivationHResult = HResult(profileActivationHResult);
                Result.ActiveProfileQueryHResult = HResult(activeResult);
                Result.ActiveLanguage = "0x" + activeLanguage.ToString("x4");
                Result.ActiveProfile = activeProfile.ToString("D").ToUpperInvariant();
            }
            catch (Exception exception)
            {
                Program.Trace(options, "profile activation exception: " + exception);
                Result.Errors.Add("Profile activation exception: " + exception);
            }

            phase = 1;
            phaseClock.Restart();
            status.Text = "Activating ContextIME TSF profile...";
            timer.Start();
            Program.Trace(options, "test timer started");
        }

        private void OnTick(object sender, EventArgs eventArgs)
        {
            try
            {
                if (lastTracedPhase != phase)
                {
                    lastTracedPhase = phase;
                    Program.Trace(options, "timer entered phase " + phase);
                }
                input.Focus();
                CaptureCandidateWindows();

                if (phase == 1 && phaseClock.ElapsedMilliseconds >= 1800)
                {
                    if (profileActivationHResult < 0)
                    {
                        Fail("ActivateLanguageProfile failed: " + HResult(profileActivationHResult));
                        return;
                    }
                    if (!AcquireForeground())
                    {
                        Fail("The smoke window could not acquire keyboard foreground focus.");
                        return;
                    }
                    if (options.Sequence != null)
                    {
                        BeginSequenceStep();
                    }
                    else
                    {
                        status.Text = "Typing pinyin and waiting for a candidate window...";
                        candidateWindowPeaks.Clear();
                        candidateBaseline = CaptureCandidateRegion(out candidateCaptureRegion);
                        Result.CandidateCaptureRegion = RectangleEvidence.FromRectangle(candidateCaptureRegion);
                        phase = 2;
                        NativeMethods.TypeAscii(options.InputText);
                        phaseClock.Restart();
                    }
                    return;
                }

                if (phase == 2)
                {
                    if (phaseClock.ElapsedMilliseconds >= 350)
                    {
                        CaptureCandidateVisualEvidence();
                    }
                    if (VisualCandidateDetected || candidateWindowPeaks.Count > 0 || phaseClock.ElapsedMilliseconds >= options.TimeoutMilliseconds)
                    {
                        SnapshotCandidateEvidence();
                        if (options.PageDownCount > 0)
                        {
                            CaptureFirstCandidatePage();
                            status.Text = "Paging the candidate list with Equal...";
                            phase = 3;
                            for (int index = 0; index < options.PageDownCount; index++)
                            {
                                NativeMethods.PressKey(NativeMethods.VirtualKeyEqual);
                            }
                            phaseClock.Restart();
                        }
                        else
                        {
                            CommitSelectedCandidate();
                        }
                    }
                    return;
                }

                if (phase == 3)
                {
                    if (phaseClock.ElapsedMilliseconds >= 250)
                    {
                        CapturePagedCandidateVisualEvidence();
                    }
                    if (phaseClock.ElapsedMilliseconds >= 800)
                    {
                        SnapshotCandidatePageEvidence();
                        CommitSelectedCandidate();
                    }
                    return;
                }

                if (phase == 4 && phaseClock.ElapsedMilliseconds >= 700)
                {
                    Result.ForegroundAfterSelection = NativeMethods.GetForegroundWindow() == Handle;
                    Result.InputFocusedAfterSelection = NativeMethods.GetFocus() == input.Handle;
                    committedText = input.Text;
                    Result.CommittedText = committedText;
                    if (!String.IsNullOrEmpty(options.FollowupInputText))
                    {
                        Result.ForegroundBeforeFollowupInput = NativeMethods.GetForegroundWindow() == Handle && NativeMethods.GetFocus() == input.Handle;
                        if (!Result.ForegroundBeforeFollowupInput)
                        {
                            Fail("The smoke window lost focus before follow-up pinyin input.");
                            return;
                        }
                        status.Text = "Typing follow-up pinyin in the same Chinese session...";
                        phase = 7;
                        NativeMethods.TypeAscii(options.FollowupInputText);
                    }
                    else
                    {
                        BeginEnglishModeCheck();
                    }
                    phaseClock.Restart();
                    return;
                }

                if (phase == 5 && phaseClock.ElapsedMilliseconds >= 450)
                {
                    phase = 6;
                    NativeMethods.TypeAscii(options.ExpectedEnglish);
                    phaseClock.Restart();
                    return;
                }

                if (phase == 6 && phaseClock.ElapsedMilliseconds >= 700)
                {
                    Complete();
                }

                if (phase == 7 && phaseClock.ElapsedMilliseconds >= 900)
                {
                    Result.ForegroundBeforeFollowupCommit = NativeMethods.GetForegroundWindow() == Handle && NativeMethods.GetFocus() == input.Handle;
                    if (!Result.ForegroundBeforeFollowupCommit)
                    {
                        Fail("The smoke window lost focus before follow-up Chinese commit.");
                        return;
                    }
                    status.Text = "Committing follow-up Chinese candidate...";
                    phase = 8;
                    NativeMethods.PressKey(NativeMethods.VirtualKeySpace);
                    phaseClock.Restart();
                    return;
                }

                if (phase == 8 && phaseClock.ElapsedMilliseconds >= 700)
                {
                    Result.FollowupCommittedText = input.Text;
                    Result.FollowupMatched = String.Equals(
                        input.Text,
                        committedText + options.ExpectedFollowupText,
                        StringComparison.Ordinal);
                    if (!Result.FollowupMatched)
                    {
                        Fail("Follow-up pinyin did not commit the expected Chinese text in the same session.");
                        return;
                    }
                    BeginEnglishModeCheck();
                    phaseClock.Restart();
                    return;
                }

                if (phase == 20)
                {
                    if (phaseClock.ElapsedMilliseconds >= 350)
                    {
                        CaptureCandidateVisualEvidence();
                    }
                    if (VisualCandidateDetected || candidateWindowPeaks.Count > 0 || phaseClock.ElapsedMilliseconds >= options.TimeoutMilliseconds)
                    {
                        SmokeSequenceStep step = options.Sequence.Steps[sequenceStepIndex];
                        phase = 21;
                        NativeMethods.PressKey(NativeMethods.SelectionVirtualKey(step.SelectionKey));
                        phaseClock.Restart();
                    }
                    return;
                }

                if (phase == 21 && phaseClock.ElapsedMilliseconds >= 700)
                {
                    CompleteSequenceStep();
                    return;
                }

                if (phase == 22 && phaseClock.ElapsedMilliseconds >= 450)
                {
                    phase = 23;
                    NativeMethods.TypeAscii(options.ExpectedEnglish);
                    phaseClock.Restart();
                    return;
                }

                if (phase == 23 && phaseClock.ElapsedMilliseconds >= 700)
                {
                    CompleteSequence();
                    return;
                }
            }
            catch (Exception exception)
            {
                Fail("Test exception: " + exception);
            }
        }

        private void CommitSelectedCandidate()
        {
            status.Text = "Committing candidate with " + options.SelectionKey + "...";
            Program.Trace(options, "committing candidate with " + options.SelectionKey);
            phase = 4;
            for (int index = 0; index < options.PreSelectionBackspaceCount; index++)
            {
                NativeMethods.PressKey(NativeMethods.VirtualKeyBack);
            }
            NativeMethods.PressKey(NativeMethods.SelectionVirtualKey(options.SelectionKey));
            phaseClock.Restart();
        }

        private void BeginEnglishModeCheck()
        {
            status.Text = "Switching with Shift, then typing deterministic English...";
            phase = 5;
            NativeMethods.PressKey(NativeMethods.VirtualKeyShift);
        }

        private void BeginSequenceStep()
        {
            SmokeSequenceStep step = options.Sequence.Steps[sequenceStepIndex];
            sequenceTextBeforeStep = input.Text;
            status.Text = "Sequence step " + (sequenceStepIndex + 1) + ": " + step.Name;
            Program.Trace(options, "starting sequence step " + sequenceStepIndex + " " + step.Name);
            ResetCandidateCapture();
            candidateBaseline = CaptureCandidateRegion(out candidateCaptureRegion);
            Result.CandidateCaptureRegion = RectangleEvidence.FromRectangle(candidateCaptureRegion);
            phase = 20;
            NativeMethods.TypeAscii(step.Input);
            phaseClock.Restart();
        }

        private void CompleteSequenceStep()
        {
            SmokeSequenceStep step = options.Sequence.Steps[sequenceStepIndex];
            SnapshotCandidateEvidence();
            string expectedText = sequenceTextBeforeStep + step.ExpectedCommit;
            bool commitMatched = String.Equals(input.Text, expectedText, StringComparison.Ordinal);
            bool candidateEvidencePersisted = !VisualCandidateDetected || candidateScreenshot != null;
            string screenshotPath = SaveSequenceCandidateScreenshot(step, sequenceStepIndex);
            bool foregroundRetained = NativeMethods.GetForegroundWindow() == Handle && NativeMethods.GetFocus() == input.Handle;
            SequenceStepResult stepResult = new SequenceStepResult
            {
                Index = sequenceStepIndex,
                Name = step.Name,
                Input = step.Input,
                SelectionKey = step.SelectionKey,
                ExpectedCommit = step.ExpectedCommit,
                TextBefore = sequenceTextBeforeStep,
                TextAfter = input.Text,
                CommitMatched = commitMatched,
                CandidateWindowDetected = Result.CandidateWindowDetected,
                CandidateDetectionMethod = Result.CandidateDetectionMethod,
                CandidateChangedPixels = Result.CandidateChangedPixels,
                CandidateScreenshotPath = screenshotPath,
                ForegroundRetained = foregroundRetained,
                Passed = commitMatched && Result.CandidateWindowDetected && candidateEvidencePersisted &&
                    foregroundRetained
            };
            Result.SequenceSteps.Add(stepResult);
            Program.Trace(options, "completed sequence step " + sequenceStepIndex + " passed=" + stepResult.Passed + " text=" + input.Text);
            if (!stepResult.Passed)
            {
                Fail("Mixed-input sequence step failed: " + step.Name);
                return;
            }

            sequenceExpectedText += step.ExpectedCommit;
            sequenceStepIndex++;
            if (sequenceStepIndex < options.Sequence.Steps.Length)
            {
                BeginSequenceStep();
                return;
            }

            Result.SequenceExpectedText = sequenceExpectedText;
            status.Text = "Sequence complete; checking Shift English mode...";
            phase = 22;
            NativeMethods.PressKey(NativeMethods.VirtualKeyShift);
            phaseClock.Restart();
        }

        private void CompleteSequence()
        {
            Program.Trace(options, "completing mixed-input sequence");
            timer.Stop();
            Result.CompletedUtc = DateTime.UtcNow.ToString("o");
            Result.FinalText = input.Text;
            committedText = Result.SequenceExpectedText;
            Result.CommittedText = committedText;
            Result.ForegroundAfterSelection = NativeMethods.GetForegroundWindow() == Handle;
            Result.InputFocusedAfterSelection = NativeMethods.GetFocus() == input.Handle;
            RestoreCapsLockState();
            Result.ServerWindows = new List<WindowEvidence>(latestServerWindows.Values).ToArray();
            Result.ProfileActivated = profileActivationHResult >= 0 &&
                String.Equals(Result.ActiveProfile, options.Profile.ToString("D"), StringComparison.OrdinalIgnoreCase);
            Result.EnglishModeMatched = String.Equals(input.Text, Result.SequenceExpectedText + options.ExpectedEnglish, StringComparison.Ordinal);
            bool allStepsPassed = Result.SequenceSteps.Count == options.Sequence.Steps.Length;
            foreach (SequenceStepResult step in Result.SequenceSteps) allStepsPassed = allStepsPassed && step.Passed;
            Result.SequenceMatched = allStepsPassed && String.Equals(committedText, Result.SequenceExpectedText, StringComparison.Ordinal);
            Result.CommitMatched = allStepsPassed;
            Result.Passed = Result.ProfileActivated && Result.CapsLockNormalized && Result.CapsLockRestored &&
                allStepsPassed && Result.EnglishModeMatched;
            if (!Result.ProfileActivated) Result.Errors.Add("ContextIME was not the active TSF profile.");
            if (!Result.CapsLockNormalized) Result.Errors.Add("Caps Lock was active while the deterministic ASCII fixture was typed.");
            if (!Result.CapsLockRestored) Result.Errors.Add("Caps Lock was not restored to its initial state.");
            if (!allStepsPassed) Result.Errors.Add("One or more mixed-input sequence steps failed.");
            if (!Result.EnglishModeMatched) Result.Errors.Add("Shift English-mode text did not match after the mixed-input sequence.");
            Close();
        }

        private void ResetCandidateCapture()
        {
            candidateWindowPeaks.Clear();
            maximumCandidateChangedPixels = 0;
            if (candidateBaseline != null) { candidateBaseline.Dispose(); candidateBaseline = null; }
            if (candidateScreenshot != null) { candidateScreenshot.Dispose(); candidateScreenshot = null; }
        }

        private string SaveSequenceCandidateScreenshot(SmokeSequenceStep step, int stepIndex)
        {
            if (candidateScreenshot == null) return null;
            string safeName = step.Name;
            foreach (char invalid in Path.GetInvalidFileNameChars()) safeName = safeName.Replace(invalid, '-');
            string path = options.EvidencePath + ".step-" + (stepIndex + 1).ToString("00") + "-" + safeName + ".candidate.png";
            candidateScreenshot.Save(path, System.Drawing.Imaging.ImageFormat.Png);
            return path;
        }

        private void CaptureCandidateWindows()
        {
            HashSet<int> serverProcessIds = new HashSet<int>();
            foreach (Process process in Process.GetProcessesByName("WeaselServer"))
            {
                serverProcessIds.Add(process.Id);
                if (!Result.ServerProcessIds.Contains(process.Id)) Result.ServerProcessIds.Add(process.Id);
                try
                {
                    string path = process.MainModule.FileName;
                    if (!Result.ServerPaths.Contains(path)) Result.ServerPaths.Add(path);
                }
                catch
                {
                }
                process.Dispose();
            }

            NativeMethods.EnumWindows(delegate(IntPtr handle, IntPtr parameter)
            {
                uint processId;
                NativeMethods.GetWindowThreadProcessId(handle, out processId);
                if (!serverProcessIds.Contains((int)processId)) return true;
                long value = handle.ToInt64();
                StringBuilder className = new StringBuilder(256);
                StringBuilder title = new StringBuilder(256);
                NativeMethods.GetClassName(handle, className, className.Capacity);
                NativeMethods.GetWindowText(handle, title, title.Capacity);
                NativeMethods.Rect rectangle;
                NativeMethods.GetWindowRect(handle, out rectangle);
                bool visible = NativeMethods.IsWindowVisible(handle);
                long style = NativeMethods.GetWindowStyle(handle);
                long extendedStyle = NativeMethods.GetWindowExtendedStyle(handle);
                bool styleVisible = (style & 0x10000000L) != 0;
                WindowEvidence window = new WindowEvidence
                {
                    Handle = "0x" + value.ToString("x"),
                    ProcessId = (int)processId,
                    ClassName = className.ToString(),
                    Title = title.ToString(),
                    Visible = visible,
                    StyleVisible = styleVisible,
                    Style = "0x" + unchecked((ulong)style).ToString("x"),
                    ExtendedStyle = "0x" + unchecked((ulong)extendedStyle).ToString("x"),
                    Left = rectangle.Left,
                    Top = rectangle.Top,
                    Width = rectangle.Right - rectangle.Left,
                    Height = rectangle.Bottom - rectangle.Top,
                    ObservedUtc = DateTime.UtcNow.ToString("o")
                };
                latestServerWindows[value] = window;
                bool candidatePanelStyle = (extendedStyle & 0x08080088L) == 0x08080088L;
                if ((phase == 2 || phase == 20) && candidatePanelStyle && styleVisible)
                {
                    WindowEvidence previous;
                    if (!candidateWindowPeaks.TryGetValue(value, out previous) || window.Width * window.Height > previous.Width * previous.Height)
                    {
                        candidateWindowPeaks[value] = window;
                    }
                }
                return true;
            }, IntPtr.Zero);
        }

        private bool AcquireForeground()
        {
            Activate();
            BringToFront();
            input.Focus();

            IntPtr foreground = NativeMethods.GetForegroundWindow();
            uint foregroundProcess;
            uint foregroundThread = foreground == IntPtr.Zero ? 0 : NativeMethods.GetWindowThreadProcessId(foreground, out foregroundProcess);
            uint currentThread = NativeMethods.GetCurrentThreadId();
            bool attached = false;
            try
            {
                if (foregroundThread != 0 && foregroundThread != currentThread)
                {
                    attached = NativeMethods.AttachThreadInput(currentThread, foregroundThread, true);
                }
                NativeMethods.BringWindowToTop(Handle);
                NativeMethods.SetActiveWindow(Handle);
                NativeMethods.SetForegroundWindow(Handle);
                NativeMethods.SetFocus(input.Handle);
            }
            finally
            {
                if (attached) NativeMethods.AttachThreadInput(currentThread, foregroundThread, false);
            }

            bool acquired = NativeMethods.GetForegroundWindow() == Handle && NativeMethods.GetFocus() == input.Handle;
            Program.Trace(options, "foreground acquisition " + acquired + ", foreground=" + NativeMethods.GetForegroundWindow().ToInt64() + ", form=" + Handle.ToInt64());
            return acquired;
        }

        private void Complete()
        {
            Program.Trace(options, "completing test");
            timer.Stop();
            Result.CompletedUtc = DateTime.UtcNow.ToString("o");
            Result.FinalText = input.Text;
            RestoreCapsLockState();
            Result.ServerWindows = new List<WindowEvidence>(latestServerWindows.Values).ToArray();
            SnapshotCandidateEvidence();
            SnapshotCandidatePageEvidence();
            SaveCandidateScreenshots();
            Result.ProfileActivated = profileActivationHResult >= 0 &&
                String.Equals(Result.ActiveProfile, options.Profile.ToString("D"), StringComparison.OrdinalIgnoreCase);
            bool cancellationExpected = String.Equals(options.SelectionKey, "escape", StringComparison.Ordinal);
            Result.CommitMatched = cancellationExpected
                ? String.Equals(committedText, options.ExpectedText, StringComparison.Ordinal)
                : String.IsNullOrEmpty(options.ExpectedText)
                    ? !String.IsNullOrEmpty(committedText)
                    : String.Equals(committedText, options.ExpectedText, StringComparison.Ordinal);
            string expectedBeforeEnglish = committedText + options.ExpectedFollowupText;
            bool followupMatched = String.IsNullOrEmpty(options.FollowupInputText) || Result.FollowupMatched;
            Result.EnglishModeMatched = String.Equals(input.Text, expectedBeforeEnglish + options.ExpectedEnglish, StringComparison.Ordinal);
            Result.Passed = Result.ProfileActivated && Result.CapsLockNormalized && Result.CapsLockRestored &&
                Result.CandidateWindowDetected && Result.CommitMatched && followupMatched && Result.EnglishModeMatched;
            bool candidateEvidencePersisted = !VisualCandidateDetected || !String.IsNullOrEmpty(Result.CandidateScreenshotPath);
            bool pageEvidencePersisted = options.PageDownCount == 0 ||
                (!String.IsNullOrEmpty(Result.FirstPageScreenshotPath) && !String.IsNullOrEmpty(Result.PagedCandidateScreenshotPath));
            Result.Passed = Result.Passed && candidateEvidencePersisted && pageEvidencePersisted &&
                (options.PageDownCount == 0 || Result.CandidatePageChanged);

            if (!Result.ProfileActivated) Result.Errors.Add("ContextIME was not the active TSF profile.");
            if (!Result.CapsLockNormalized) Result.Errors.Add("Caps Lock was active while the deterministic ASCII fixture was typed.");
            if (!Result.CapsLockRestored) Result.Errors.Add("Caps Lock was not restored to its initial state.");
            if (!Result.CandidateWindowDetected) Result.Errors.Add("No candidate UI visual change or candidate panel window was observed before commit.");
            if (!candidateEvidencePersisted) Result.Errors.Add("Candidate UI changed visually, but its screenshot was not persisted.");
            if (options.PageDownCount > 0 && !Result.CandidatePageChanged) Result.Errors.Add("The candidate UI did not change after paging.");
            if (!pageEvidencePersisted) Result.Errors.Add("Candidate paging screenshots were not persisted.");
            if (!Result.CommitMatched) Result.Errors.Add("Committed text did not match the expected fixture.");
            if (!followupMatched) Result.Errors.Add("Follow-up pinyin did not commit the expected Chinese fixture.");
            if (!Result.EnglishModeMatched) Result.Errors.Add("Shift English-mode text did not match the expected fixture.");
            Close();
        }

        private void Fail(string error)
        {
            Program.Trace(options, "failing test: " + error);
            Result.Errors.Add(error);
            Result.CompletedUtc = DateTime.UtcNow.ToString("o");
            Result.CommittedText = committedText;
            Result.FinalText = input.Text;
            Result.ServerWindows = new List<WindowEvidence>(latestServerWindows.Values).ToArray();
            CaptureCandidateVisualEvidence();
            CapturePagedCandidateVisualEvidence();
            SnapshotCandidateEvidence();
            SnapshotCandidatePageEvidence();
            timer.Stop();
            RestoreCapsLockState();
            if (!Result.CapsLockRestored) Result.Errors.Add("Caps Lock was not restored to its initial state after failure.");
            Result.Passed = false;
            SaveCandidateScreenshots();
            Close();
        }

        private void NormalizeCapsLockState()
        {
            Result.CapsLockInitiallyOn = NativeMethods.IsToggleKeyOn(NativeMethods.VirtualKeyCapital);
            capsLockStateCaptured = true;
            if (Result.CapsLockInitiallyOn)
            {
                NativeMethods.PressKey(NativeMethods.VirtualKeyCapital);
                Application.DoEvents();
            }
            Result.CapsLockNormalized = !NativeMethods.IsToggleKeyOn(NativeMethods.VirtualKeyCapital);
            Program.Trace(options, "Caps Lock normalized=" + Result.CapsLockNormalized + ", initiallyOn=" + Result.CapsLockInitiallyOn);
        }

        private void RestoreCapsLockState()
        {
            if (!capsLockStateCaptured) return;
            bool current = NativeMethods.IsToggleKeyOn(NativeMethods.VirtualKeyCapital);
            if (current != Result.CapsLockInitiallyOn)
            {
                NativeMethods.PressKey(NativeMethods.VirtualKeyCapital);
                Application.DoEvents();
            }
            Result.CapsLockRestored = NativeMethods.IsToggleKeyOn(NativeMethods.VirtualKeyCapital) == Result.CapsLockInitiallyOn;
        }

        private void SnapshotCandidateEvidence()
        {
            Result.CandidateWindowDetected = VisualCandidateDetected || candidateWindowPeaks.Count > 0;
            Result.CandidateDetectionMethod = VisualCandidateDetected ? "screen-pixel-delta" : (candidateWindowPeaks.Count > 0 ? "server-window-style" : "none");
            Result.CandidateChangedPixels = maximumCandidateChangedPixels;
            Result.CandidateWindows = new List<WindowEvidence>(candidateWindowPeaks.Values).ToArray();
        }

        private void SnapshotCandidatePageEvidence()
        {
            Result.CandidatePageChangedPixels = maximumPageChangedPixels;
            // A page transition only redraws the candidate glyphs inside an
            // already-visible panel, so it changes fewer pixels than the
            // initial panel appearance at high DPI.
            Result.CandidatePageChanged = maximumPageChangedPixels >= CandidatePageChangePixelThreshold;
        }

        private bool VisualCandidateDetected
        {
            get { return maximumCandidateChangedPixels >= CandidateAppearancePixelThreshold; }
        }

        private Bitmap CaptureCandidateRegion(out Rectangle region)
        {
            Point inputBottomLeft = input.PointToScreen(new Point(0, input.Height + 3));
            Rectangle safeClient = RectangleToScreen(ClientRectangle);
            // A long mixed-input sequence horizontally scrolls the single-line
            // TextBox and moves the candidate panel toward its right edge. Keep
            // the full client width in the baseline so later steps remain
            // observable instead of inheriting the original 480 px fixture.
            int left = safeClient.Left;
            int top = Math.Max(safeClient.Top, Math.Min(inputBottomLeft.Y, safeClient.Bottom - 1));
            int width = safeClient.Right - left;
            int height = Math.Min(380, safeClient.Bottom - top);
            region = new Rectangle(left, top, Math.Max(width, 1), Math.Max(height, 1));

            Bitmap bitmap = new Bitmap(region.Width, region.Height, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (Graphics graphics = Graphics.FromImage(bitmap))
            {
                graphics.CopyFromScreen(region.Location, Point.Empty, region.Size, CopyPixelOperation.SourceCopy);
            }
            return bitmap;
        }

        private void CaptureCandidateVisualEvidence()
        {
            if ((phase != 2 && phase != 20) || candidateBaseline == null) return;
            Rectangle currentRegion;
            using (Bitmap current = CaptureCandidateRegion(out currentRegion))
            {
                if (currentRegion != candidateCaptureRegion || current.Size != candidateBaseline.Size) return;
                int changedPixels = CountChangedPixels(candidateBaseline, current);
                if (changedPixels > maximumCandidateChangedPixels)
                {
                    maximumCandidateChangedPixels = changedPixels;
                    if (candidateScreenshot != null) candidateScreenshot.Dispose();
                    candidateScreenshot = (Bitmap)current.Clone();
                }
            }
        }

        private void CaptureFirstCandidatePage()
        {
            Rectangle currentRegion;
            using (Bitmap current = CaptureCandidateRegion(out currentRegion))
            {
                if (currentRegion != candidateCaptureRegion || current.Size != candidateBaseline.Size) return;
                if (firstPageScreenshot != null) firstPageScreenshot.Dispose();
                firstPageScreenshot = (Bitmap)current.Clone();
            }
        }

        private void CapturePagedCandidateVisualEvidence()
        {
            if (phase != 3 || firstPageScreenshot == null) return;
            Rectangle currentRegion;
            using (Bitmap current = CaptureCandidateRegion(out currentRegion))
            {
                if (currentRegion != candidateCaptureRegion || current.Size != firstPageScreenshot.Size) return;
                int changedPixels = CountChangedPixels(firstPageScreenshot, current);
                if (changedPixels > maximumPageChangedPixels)
                {
                    maximumPageChangedPixels = changedPixels;
                    if (pagedCandidateScreenshot != null) pagedCandidateScreenshot.Dispose();
                    pagedCandidateScreenshot = (Bitmap)current.Clone();
                }
            }
        }

        private static int CountChangedPixels(Bitmap baseline, Bitmap current)
        {
            Rectangle rectangle = new Rectangle(Point.Empty, baseline.Size);
            System.Drawing.Imaging.BitmapData baselineData = baseline.LockBits(rectangle, System.Drawing.Imaging.ImageLockMode.ReadOnly, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            System.Drawing.Imaging.BitmapData currentData = current.LockBits(rectangle, System.Drawing.Imaging.ImageLockMode.ReadOnly, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            try
            {
                int byteCount = Math.Abs(baselineData.Stride) * baseline.Height;
                byte[] before = new byte[byteCount];
                byte[] after = new byte[byteCount];
                Marshal.Copy(baselineData.Scan0, before, 0, byteCount);
                Marshal.Copy(currentData.Scan0, after, 0, byteCount);
                int changed = 0;
                for (int index = 0; index + 3 < byteCount; index += 4)
                {
                    int difference = Math.Abs(before[index] - after[index]) +
                        Math.Abs(before[index + 1] - after[index + 1]) +
                        Math.Abs(before[index + 2] - after[index + 2]);
                    if (difference >= 45) changed++;
                }
                return changed;
            }
            finally
            {
                baseline.UnlockBits(baselineData);
                current.UnlockBits(currentData);
            }
        }

        private void SaveCandidateScreenshots()
        {
            if (candidateScreenshot != null)
            {
                try
                {
                    string path = options.EvidencePath + ".candidate.png";
                    candidateScreenshot.Save(path, System.Drawing.Imaging.ImageFormat.Png);
                    Result.CandidateScreenshotPath = path;
                }
                catch (Exception exception)
                {
                    Result.Errors.Add("Could not save candidate screenshot: " + exception.Message);
                }
            }

            if (firstPageScreenshot != null)
            {
                try
                {
                    string path = options.EvidencePath + ".candidate-page-1.png";
                    firstPageScreenshot.Save(path, System.Drawing.Imaging.ImageFormat.Png);
                    Result.FirstPageScreenshotPath = path;
                }
                catch (Exception exception)
                {
                    Result.Errors.Add("Could not save the first candidate page screenshot: " + exception.Message);
                }
            }

            if (pagedCandidateScreenshot != null)
            {
                try
                {
                    string path = options.EvidencePath + ".candidate-page-2.png";
                    pagedCandidateScreenshot.Save(path, System.Drawing.Imaging.ImageFormat.Png);
                    Result.PagedCandidateScreenshotPath = path;
                }
                catch (Exception exception)
                {
                    Result.Errors.Add("Could not save the paged candidate screenshot: " + exception.Message);
                }
            }
        }

        private static string HResult(int value)
        {
            return "0x" + unchecked((uint)value).ToString("x8");
        }
    }

    internal sealed class SmokeResult
    {
        public string SchemaVersion;
        public string StartedUtc;
        public string CompletedUtc;
        public string OsVersion;
        public string WindowsProductName;
        public string WindowsDisplayVersion;
        public string WindowsBuild;
        public string MachineName;
        public string TextService;
        public string RequestedProfile;
        public string ActiveProfile;
        public string ActiveLanguage;
        public string ProfileActivationHResult;
        public string ActiveProfileQueryHResult;
        public string SequencePath;
        public string SequenceName;
        public string SequenceExpectedText;
        public bool SequenceMatched;
        public List<SequenceStepResult> SequenceSteps;
        public string InputText;
        public string ExpectedText;
        public string CommittedText;
        public string ExpectedEnglish;
        public string FollowupInputText;
        public string ExpectedFollowupText;
        public string FollowupCommittedText;
        public string SelectionKey;
        public int PreSelectionBackspaceCount;
        public int PageDownCount;
        public string PageDownKey;
        public string FinalText;
        public bool ProfileActivated;
        public bool CapsLockInitiallyOn;
        public bool CapsLockNormalized;
        public bool CapsLockRestored;
        public bool ForegroundAcquired;
        public bool ForegroundAfterSelection;
        public bool InputFocusedAfterSelection;
        public bool CandidateWindowDetected;
        public string CandidateDetectionMethod;
        public int CandidateChangedPixels;
        public string CandidateScreenshotPath;
        public RectangleEvidence CandidateCaptureRegion;
        public bool CandidatePageChanged;
        public int CandidatePageChangedPixels;
        public string FirstPageScreenshotPath;
        public string PagedCandidateScreenshotPath;
        public bool CommitMatched;
        public bool ForegroundBeforeFollowupInput;
        public bool ForegroundBeforeFollowupCommit;
        public bool FollowupMatched;
        public bool EnglishModeMatched;
        public bool Passed;
        public List<int> ServerProcessIds;
        public List<string> ServerPaths;
        public WindowEvidence[] CandidateWindows;
        public WindowEvidence[] ServerWindows;
        public List<string> Errors;

        public static SmokeResult Create(SmokeOptions options)
        {
            return new SmokeResult
            {
                SchemaVersion = "contextime.tsf-smoke.v6",
                StartedUtc = DateTime.UtcNow.ToString("o"),
                OsVersion = Environment.OSVersion.VersionString,
                WindowsProductName = RegistryValue("ProductName"),
                WindowsDisplayVersion = RegistryValue("DisplayVersion"),
                WindowsBuild = RegistryValue("CurrentBuildNumber") + "." + RegistryValue("UBR"),
                MachineName = Environment.MachineName,
                TextService = options.TextService.ToString("D").ToUpperInvariant(),
                RequestedProfile = options.Profile.ToString("D").ToUpperInvariant(),
                SequencePath = options.SequencePath,
                SequenceName = options.Sequence == null ? String.Empty : options.Sequence.Name,
                SequenceSteps = new List<SequenceStepResult>(),
                InputText = options.InputText,
                ExpectedText = options.ExpectedText,
                ExpectedEnglish = options.ExpectedEnglish,
                FollowupInputText = options.FollowupInputText,
                ExpectedFollowupText = options.ExpectedFollowupText,
                SelectionKey = options.SelectionKey,
                PreSelectionBackspaceCount = options.PreSelectionBackspaceCount,
                PageDownCount = options.PageDownCount,
                PageDownKey = options.PageDownCount > 0 ? "equal" : String.Empty,
                ServerProcessIds = new List<int>(),
                ServerPaths = new List<string>(),
                CandidateWindows = new WindowEvidence[0],
                ServerWindows = new WindowEvidence[0],
                Errors = new List<string>()
            };
        }

        private static string RegistryValue(string name)
        {
            object value = Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion", name, String.Empty);
            return value == null ? String.Empty : Convert.ToString(value);
        }
    }

    internal sealed class SequenceStepResult
    {
        public int Index;
        public string Name;
        public string Input;
        public string SelectionKey;
        public string ExpectedCommit;
        public string TextBefore;
        public string TextAfter;
        public bool CommitMatched;
        public bool CandidateWindowDetected;
        public string CandidateDetectionMethod;
        public int CandidateChangedPixels;
        public string CandidateScreenshotPath;
        public bool ForegroundRetained;
        public bool Passed;
    }

    internal sealed class WindowEvidence
    {
        public string Handle;
        public int ProcessId;
        public string ClassName;
        public string Title;
        public bool Visible;
        public bool StyleVisible;
        public string Style;
        public string ExtendedStyle;
        public int Left;
        public int Top;
        public int Width;
        public int Height;
        public string ObservedUtc;
    }

    internal sealed class RectangleEvidence
    {
        public int Left;
        public int Top;
        public int Width;
        public int Height;

        public static RectangleEvidence FromRectangle(Rectangle rectangle)
        {
            return new RectangleEvidence
            {
                Left = rectangle.Left,
                Top = rectangle.Top,
                Width = rectangle.Width,
                Height = rectangle.Height
            };
        }
    }

    [ComImport]
    [Guid("1F02B6C5-7842-4EE6-8A0B-9A24183A95CA")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface ITfInputProcessorProfiles
    {
        [PreserveSig] int Register(ref Guid classId);
        [PreserveSig] int Unregister(ref Guid classId);
        [PreserveSig] int AddLanguageProfile(ref Guid classId, ushort languageId, ref Guid profile, [MarshalAs(UnmanagedType.LPWStr)] string description, uint descriptionLength, [MarshalAs(UnmanagedType.LPWStr)] string iconFile, uint iconFileLength, uint iconIndex);
        [PreserveSig] int RemoveLanguageProfile(ref Guid classId, ushort languageId, ref Guid profile);
        [PreserveSig] int EnumInputProcessorInfo([MarshalAs(UnmanagedType.Interface)] out object enumerator);
        [PreserveSig] int GetDefaultLanguageProfile(ushort languageId, ref Guid category, out Guid classId, out Guid profile);
        [PreserveSig] int SetDefaultLanguageProfile(ushort languageId, ref Guid category, ref Guid classId, ref Guid profile);
        [PreserveSig] int ActivateLanguageProfile(ref Guid classId, ushort languageId, ref Guid profile);
        [PreserveSig] int GetActiveLanguageProfile(ref Guid classId, out ushort languageId, out Guid profile);
    }

    internal static class NativeMethods
    {
        internal const byte VirtualKeyShift = 0x10;
        internal const byte VirtualKeyCapital = 0x14;
        internal const byte VirtualKeySpace = 0x20;
        internal const byte VirtualKeyReturn = 0x0D;
        internal const byte VirtualKeyEscape = 0x1B;
        internal const byte VirtualKeyBack = 0x08;
        internal const byte VirtualKeyEqual = 0xBB;
        private const uint KeyEventKeyUp = 0x0002;

        internal delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);

        [StructLayout(LayoutKind.Sequential)]
        internal struct Rect
        {
            internal int Left;
            internal int Top;
            internal int Right;
            internal int Bottom;
        }

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool SetForegroundWindow(IntPtr window);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool SetProcessDPIAware();

        [DllImport("user32.dll")]
        internal static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll")]
        internal static extern IntPtr GetFocus();

        [DllImport("user32.dll")]
        internal static extern short GetKeyState(int virtualKey);

        [DllImport("user32.dll")]
        internal static extern IntPtr SetFocus(IntPtr window);

        [DllImport("user32.dll")]
        internal static extern IntPtr SetActiveWindow(IntPtr window);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool BringWindowToTop(IntPtr window);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool AttachThreadInput(uint attachThread, uint attachToThread, [MarshalAs(UnmanagedType.Bool)] bool attach);

        [DllImport("kernel32.dll")]
        internal static extern uint GetCurrentThreadId();

        [DllImport("user32.dll")]
        internal static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

        [DllImport("user32.dll")]
        internal static extern uint MapVirtualKey(uint code, uint mapType);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        internal static extern short VkKeyScan(char character);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr parameter);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool IsWindowVisible(IntPtr window);

        [DllImport("user32.dll")]
        internal static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        internal static extern int GetClassName(IntPtr window, StringBuilder className, int maximumCount);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        internal static extern int GetWindowText(IntPtr window, StringBuilder title, int maximumCount);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool GetWindowRect(IntPtr window, out Rect rectangle);

        [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
        private static extern IntPtr GetWindowLongPtr64(IntPtr window, int index);

        [DllImport("user32.dll", EntryPoint = "GetWindowLongW")]
        private static extern IntPtr GetWindowLong32(IntPtr window, int index);

        internal static long GetWindowStyle(IntPtr window)
        {
            return (IntPtr.Size == 8 ? GetWindowLongPtr64(window, -16) : GetWindowLong32(window, -16)).ToInt64();
        }

        internal static long GetWindowExtendedStyle(IntPtr window)
        {
            return (IntPtr.Size == 8 ? GetWindowLongPtr64(window, -20) : GetWindowLong32(window, -20)).ToInt64();
        }

        internal static void PressKey(byte virtualKey)
        {
            byte scanCode = (byte)MapVirtualKey(virtualKey, 0);
            keybd_event(virtualKey, scanCode, 0, UIntPtr.Zero);
            Thread.Sleep(35);
            keybd_event(virtualKey, scanCode, KeyEventKeyUp, UIntPtr.Zero);
            Thread.Sleep(50);
        }

        internal static bool IsToggleKeyOn(byte virtualKey)
        {
            return (GetKeyState(virtualKey) & 0x0001) != 0;
        }

        internal static void PressChord(byte modifier, byte virtualKey)
        {
            byte modifierScanCode = (byte)MapVirtualKey(modifier, 0);
            byte keyScanCode = (byte)MapVirtualKey(virtualKey, 0);
            keybd_event(modifier, modifierScanCode, 0, UIntPtr.Zero);
            Thread.Sleep(35);
            keybd_event(virtualKey, keyScanCode, 0, UIntPtr.Zero);
            Thread.Sleep(35);
            keybd_event(virtualKey, keyScanCode, KeyEventKeyUp, UIntPtr.Zero);
            Thread.Sleep(35);
            keybd_event(modifier, modifierScanCode, KeyEventKeyUp, UIntPtr.Zero);
            Thread.Sleep(75);
        }

        internal static byte SelectionVirtualKey(string selectionKey)
        {
            if (String.Equals(selectionKey, "space", StringComparison.Ordinal)) return VirtualKeySpace;
            if (String.Equals(selectionKey, "enter", StringComparison.Ordinal)) return VirtualKeyReturn;
            if (String.Equals(selectionKey, "escape", StringComparison.Ordinal)) return VirtualKeyEscape;
            return (byte)selectionKey[0];
        }

        internal static void TypeAscii(string text)
        {
            foreach (char character in text)
            {
                if (character > 0x7f || Char.IsControl(character))
                {
                    throw new ArgumentException("The deterministic fixture only supports printable ASCII characters.");
                }
                short mapping = VkKeyScan(character);
                if (mapping == -1)
                {
                    throw new ArgumentException("The current keyboard layout cannot type ASCII character: " + character);
                }
                byte virtualKey = (byte)(mapping & 0xff);
                byte modifiers = (byte)((mapping >> 8) & 0xff);
                if (modifiers == 0)
                {
                    PressKey(virtualKey);
                }
                else if (modifiers == 1)
                {
                    PressChord(VirtualKeyShift, virtualKey);
                }
                else
                {
                    throw new ArgumentException("The deterministic fixture does not use Ctrl or Alt ASCII chords.");
                }
            }
        }
    }
}
