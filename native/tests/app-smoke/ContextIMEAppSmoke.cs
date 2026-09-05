using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Automation;

namespace ContextIME.NativeTests
{
    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            AppSmokeOptions options;
            try
            {
                options = AppSmokeOptions.Parse(args);
            }
            catch (Exception exception)
            {
                File.WriteAllText(Path.Combine(Path.GetTempPath(), "contextime-app-smoke-startup-error.txt"), exception.ToString());
                return 2;
            }

            AppSmokeResult result = AppSmokeRunner.Run(options);
            try
            {
                string parent = Path.GetDirectoryName(options.EvidencePath);
                if (!String.IsNullOrEmpty(parent)) Directory.CreateDirectory(parent);
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

        internal static void Trace(AppSmokeOptions options, string message)
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

    internal sealed class AppSmokeOptions
    {
        public string EvidencePath;
        public string Application = "notepad";
        public string Scenario = "baseline";
        public string InputText = "shurufa";
        public string ExpectedText = "输入法";
        public string ExpectedEnglish = "abc";
        public string VSCodeExtensionPath;
        public int TimeoutMilliseconds = 20000;
        public Guid TextService = new Guid("9FA3541F-F3F9-4C67-AA42-6C9AB15FB6A9");
        public Guid Profile = new Guid("A200BA94-B1A7-4668-A22B-CA61EC1E79F7");
        public ushort Language = 0x0804;

        public static AppSmokeOptions Parse(string[] args)
        {
            AppSmokeOptions options = new AppSmokeOptions();
            for (int index = 0; index < args.Length; index++)
            {
                string name = args[index];
                if (index + 1 >= args.Length) throw new ArgumentException("Missing value for " + name);
                string value = args[++index];
                if (name == "--evidence") options.EvidencePath = Path.GetFullPath(value);
                else if (name == "--application") options.Application = value.ToLowerInvariant();
                else if (name == "--scenario") options.Scenario = value.ToLowerInvariant();
                else if (name == "--input") options.InputText = value;
                else if (name == "--expected") options.ExpectedText = value;
                else if (name == "--expected-english") options.ExpectedEnglish = value;
                else if (name == "--vscode-extension") options.VSCodeExtensionPath = Path.GetFullPath(value);
                else if (name == "--timeout-ms") options.TimeoutMilliseconds = Int32.Parse(value);
                else if (name == "--text-service") options.TextService = new Guid(value);
                else if (name == "--profile") options.Profile = new Guid(value);
                else if (name == "--language") options.Language = UInt16.Parse(value);
                else throw new ArgumentException("Unknown argument: " + name);
            }

            if (String.IsNullOrEmpty(options.EvidencePath)) throw new ArgumentException("--evidence is required.");
            string[] applications = { "notepad", "browser", "vscode", "visualstudio", "terminal" };
            if (Array.IndexOf(applications, options.Application) < 0) throw new ArgumentException("Unknown application adapter: " + options.Application);
            string[] scenarios = { "baseline", "terminal-context", "vscode-context", "vscode-project-candidate" };
            if (Array.IndexOf(scenarios, options.Scenario) < 0) throw new ArgumentException("Unknown smoke scenario: " + options.Scenario);
            if (options.Scenario == "terminal-context" && options.Application != "terminal") throw new ArgumentException("The terminal-context scenario requires --application terminal.");
            if (options.Scenario == "vscode-context" && options.Application != "vscode") throw new ArgumentException("The vscode-context scenario requires --application vscode.");
            if (options.Scenario == "vscode-context" && (String.IsNullOrEmpty(options.VSCodeExtensionPath) || !File.Exists(options.VSCodeExtensionPath))) throw new ArgumentException("The vscode-context scenario requires an existing --vscode-extension file.");
            if (options.Scenario == "vscode-project-candidate" && options.Application != "vscode") throw new ArgumentException("The vscode-project-candidate scenario requires --application vscode.");
            if (options.Scenario == "vscode-project-candidate" && (String.IsNullOrEmpty(options.VSCodeExtensionPath) || !File.Exists(options.VSCodeExtensionPath))) throw new ArgumentException("The vscode-project-candidate scenario requires an existing --vscode-extension file.");
            if (String.IsNullOrEmpty(options.InputText)) throw new ArgumentException("--input cannot be empty.");
            if (String.IsNullOrEmpty(options.ExpectedText)) throw new ArgumentException("--expected cannot be empty.");
            if (String.IsNullOrEmpty(options.ExpectedEnglish)) throw new ArgumentException("--expected-english cannot be empty.");
            if (options.TimeoutMilliseconds < 5000 || options.TimeoutMilliseconds > 120000) throw new ArgumentOutOfRangeException("--timeout-ms");
            return options;
        }
    }

    internal static class AppSmokeRunner
    {
        private static readonly Guid InputProcessorProfilesClass = new Guid("33C53A50-F456-4884-B049-85FD643ECFED");
        private static readonly Guid KeyboardCategory = new Guid("34745C63-B2F0-4784-8B67-5E12C8701A31");
        private const uint ProfileTypeInputProcessor = 0x0001;
        private const uint ActivateForSession = 0x20000000;
        private const uint DontCareCurrentInputLanguage = 0x00000004;

        public static AppSmokeResult Run(AppSmokeOptions options)
        {
            AppSmokeResult result = AppSmokeResult.Create(options);
            ITfInputProcessorProfileMgr profileManager = null;
            TfInputProcessorProfile originalProfile = new TfInputProcessorProfile();
            bool originalProfileCaptured = false;
            IAppProbe probe = null;
            Bitmap baseline = null;
            Bitmap candidate = null;

            try
            {
                Program.Trace(options, "creating input processor profile manager");
                Type type = Type.GetTypeFromCLSID(InputProcessorProfilesClass, true);
                profileManager = (ITfInputProcessorProfileMgr)Activator.CreateInstance(type);
                Guid keyboardCategory = KeyboardCategory;
                int getOriginalResult = profileManager.GetActiveProfile(ref keyboardCategory, out originalProfile);
                result.OriginalProfileQueryHResult = HResult(getOriginalResult);
                originalProfileCaptured = getOriginalResult >= 0;
                if (originalProfileCaptured) result.OriginalProfile = ProfileEvidence.FromProfile(originalProfile);

                Guid service = options.TextService;
                Guid profile = options.Profile;
                int activationResult = profileManager.ActivateProfile(
                    ProfileTypeInputProcessor,
                    options.Language,
                    ref service,
                    ref profile,
                    IntPtr.Zero,
                    ActivateForSession | DontCareCurrentInputLanguage);
                result.ProfileActivationHResult = HResult(activationResult);
                if (activationResult < 0) throw new InvalidOperationException("Session profile activation failed: " + HResult(activationResult));

                TfInputProcessorProfile activeProfile;
                keyboardCategory = KeyboardCategory;
                int getActiveResult = profileManager.GetActiveProfile(ref keyboardCategory, out activeProfile);
                result.ActiveProfileQueryHResult = HResult(getActiveResult);
                if (getActiveResult >= 0) result.ActiveProfile = ProfileEvidence.FromProfile(activeProfile);
                result.ProfileActivated = getActiveResult >= 0 &&
                    activeProfile.ProfileType == ProfileTypeInputProcessor &&
                    activeProfile.ClassId == options.TextService &&
                    activeProfile.Profile == options.Profile;
                if (!result.ProfileActivated) throw new InvalidOperationException("ContextIME was not the active desktop keyboard profile.");

                if (options.Application == "notepad") probe = NotepadProbe.Open(options, result);
                else if (options.Application == "browser") probe = BrowserProbe.Open(options, result);
                else if (options.Application == "vscode") probe = VsCodeProbe.Open(options, result);
                else if (options.Application == "visualstudio") probe = VisualStudioProbe.Open(options, result);
                else if (options.Application == "terminal") probe = TerminalProbe.Open(options, result);
                else throw new InvalidOperationException("No adapter exists for " + options.Application + ".");
                result.ForegroundAcquired = probe.AcquireForeground();
                result.RequestedWindowHandle = Handle(probe.WindowHandle);
                result.ForegroundWindowHandleAfterAcquire = Handle(NativeMethods.GetForegroundWindow());
                try
                {
                    AutomationElement focusedElement = AutomationElement.FocusedElement;
                    result.FocusedElementNameAfterAcquire = focusedElement.Current.Name;
                    result.FocusedElementProcessIdAfterAcquire = focusedElement.Current.ProcessId;
                }
                catch
                {
                }
                if (!result.ForegroundAcquired) throw new InvalidOperationException("The " + options.Application + " probe could not acquire foreground focus.");

                result.InitialCompositionDismissed = DismissInitialComposition(probe, result);
                if (!result.InitialCompositionDismissed) throw new InvalidOperationException("The " + options.Application + " probe lost foreground focus while dismissing initial composition.");
                probe.PrepareFixture();
                result.FixturePrepared = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                if (!result.FixturePrepared) throw new InvalidOperationException("The " + options.Application + " probe lost foreground focus while preparing its fixture.");

                if (options.Scenario == "terminal-context")
                {
                    RunTerminalContextScenario(options, probe, result);
                }
                else if (options.Scenario == "vscode-context")
                {
                    RunVsCodeContextScenario(options, probe, result);
                }
                else if (options.Scenario == "vscode-project-candidate")
                {
                    RunVsCodeProjectCandidateScenario(options, probe, result);
                }
                else
                {
                Thread.Sleep(500);
                baseline = Capture(probe.CaptureRegion);
                result.ForegroundBeforePinyinInput = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                if (!result.ForegroundBeforePinyinInput) throw new InvalidOperationException("The " + options.Application + " probe lost foreground focus before pinyin input.");
                NativeMethods.TypeAscii(options.InputText);
                Thread.Sleep(1000);
                result.ForegroundBeforeCandidateCapture = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                if (!result.ForegroundBeforeCandidateCapture) throw new InvalidOperationException("The " + options.Application + " probe lost foreground focus before candidate capture.");
                candidate = Capture(probe.CaptureRegion);
                result.ForegroundAfterCandidateCapture = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                if (!result.ForegroundAfterCandidateCapture) throw new InvalidOperationException("The " + options.Application + " probe lost foreground focus during candidate capture.");
                result.CandidateChangedPixels = CountChangedPixels(baseline, candidate);
                string candidatePath = options.EvidencePath + ".candidate.png";
                candidate.Save(candidatePath, System.Drawing.Imaging.ImageFormat.Png);
                result.CandidateScreenshotPath = candidatePath;
                result.CandidateWindowDetected = result.CandidateChangedPixels >= 1500;

                result.ForegroundBeforeCommit = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                if (!result.ForegroundBeforeCommit) throw new InvalidOperationException("The " + options.Application + " probe lost foreground focus before candidate commit.");
                NativeMethods.PressKey(NativeMethods.VirtualKeySpace);
                Thread.Sleep(800);
                result.CommittedText = probe.ReadText();
                result.CommitMatched = probe.TextMatches(result.CommittedText, options.ExpectedText);

                result.ForegroundBeforeEnglishSwitch = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                if (!result.ForegroundBeforeEnglishSwitch) throw new InvalidOperationException("The " + options.Application + " probe lost foreground focus before the English-mode switch.");
                NativeMethods.PressKey(NativeMethods.VirtualKeyShift);
                Thread.Sleep(450);
                result.ForegroundBeforeEnglishInput = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                if (!result.ForegroundBeforeEnglishInput) throw new InvalidOperationException("The " + options.Application + " probe lost foreground focus before English input.");
                NativeMethods.TypeAscii(options.ExpectedEnglish);
                Thread.Sleep(700);
                result.FinalText = probe.ReadText();
                result.EnglishModeMatched = probe.TextMatches(result.FinalText, options.ExpectedText + options.ExpectedEnglish);
                result.ForegroundAfterInput = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                bool directEnglishObserved = result.FinalText != null && result.FinalText.EndsWith(options.ExpectedEnglish, StringComparison.Ordinal);
                if (directEnglishObserved && result.ForegroundAfterInput)
                {
                    NativeMethods.PressKey(NativeMethods.VirtualKeyShift);
                    Thread.Sleep(450);
                    result.ChineseModeRestored = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                }
                result.Passed = result.ProfileActivated && result.ForegroundAcquired && result.InitialCompositionDismissed && result.FixturePrepared &&
                    result.ForegroundBeforePinyinInput && result.ForegroundBeforeCandidateCapture && result.ForegroundAfterCandidateCapture &&
                    result.CandidateWindowDetected && result.ForegroundBeforeCommit && result.ForegroundBeforeEnglishSwitch && result.ForegroundBeforeEnglishInput &&
                    result.CommitMatched && result.EnglishModeMatched && result.ForegroundAfterInput && result.ChineseModeRestored;

                if (!result.CandidateWindowDetected) result.Errors.Add("No significant candidate/composition visual change was detected.");
                if (!result.CommitMatched) result.Errors.Add("Committed text did not match the expected fixture.");
                if (!result.EnglishModeMatched) result.Errors.Add("Shift English-mode text did not match the expected fixture.");
                if (!result.ForegroundAfterInput) result.Errors.Add("The " + options.Application + " probe lost foreground focus during the smoke test.");
                if (result.EnglishModeMatched && !result.ChineseModeRestored) result.Errors.Add("Chinese mode was not restored after the English-mode check.");
                }
            }
            catch (Exception exception)
            {
                Program.Trace(options, "test failed: " + exception);
                result.Errors.Add(exception.ToString());
                result.Passed = false;
            }
            finally
            {
                if (baseline != null) baseline.Dispose();
                if (candidate != null) candidate.Dispose();
                if (probe != null)
                {
                    try
                    {
                        result.CleanupForegroundAcquired = AcquireForegroundForCleanup(probe, result);
                        if (!result.CleanupForegroundAcquired) throw new InvalidOperationException("The probe could not reacquire foreground focus for cleanup.");
                        NativeMethods.PressKey(NativeMethods.VirtualKeyEscape);
                        Thread.Sleep(150);
                        result.CompositionDismissedBeforeClose = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                        if (!result.CompositionDismissedBeforeClose) throw new InvalidOperationException("The probe lost foreground focus while dismissing composition for cleanup.");
                        probe.Close();
                        result.ProbeClosed = true;
                    }
                    catch (Exception exception)
                    {
                        result.Errors.Add("Probe cleanup failed: " + exception);
                        result.Passed = false;
                    }
                }

                if (profileManager != null && originalProfileCaptured)
                {
                    try
                    {
                        Guid originalClass = originalProfile.ClassId;
                        Guid originalProfileId = originalProfile.Profile;
                        int restoreResult = profileManager.ActivateProfile(
                            originalProfile.ProfileType,
                            originalProfile.Language,
                            ref originalClass,
                            ref originalProfileId,
                            originalProfile.KeyboardLayout,
                            ActivateForSession | DontCareCurrentInputLanguage);
                        result.ProfileRestoreHResult = HResult(restoreResult);
                        result.ProfileRestored = restoreResult >= 0;
                        if (!result.ProfileRestored)
                        {
                            result.Errors.Add("Original desktop keyboard profile restore failed: " + HResult(restoreResult));
                            result.Passed = false;
                        }
                    }
                    catch (Exception exception)
                    {
                        result.Errors.Add("Original desktop keyboard profile restore failed: " + exception.Message);
                        result.Passed = false;
                    }
                }

                if (profileManager != null && Marshal.IsComObject(profileManager)) Marshal.FinalReleaseComObject(profileManager);
                result.CompletedUtc = DateTime.UtcNow.ToString("o");
            }

            return result;
        }

        private static void RunTerminalContextScenario(AppSmokeOptions options, IAppProbe probe, AppSmokeResult result)
        {
            const int candidateThreshold = 1500;
            const int contextSettleMilliseconds = 1500;

            Thread.Sleep(contextSettleMilliseconds);
            result.ForegroundBeforePinyinInput = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            if (!result.ForegroundBeforePinyinInput)
            {
                result.Errors.Add("Terminal lost foreground focus before the default-KEEP probe.");
                return;
            }

            using (Bitmap clean = Capture(probe.CaptureRegion))
            {
                NativeMethods.TypeAscii(options.InputText);
                Thread.Sleep(750);
                using (Bitmap candidate = Capture(probe.CaptureRegion))
                {
                    result.ForegroundBeforeCandidateCapture = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                    result.CandidateChangedPixels = CountChangedPixels(clean, candidate);
                    result.CandidateScreenshotPath = options.EvidencePath + ".candidate.png";
                    candidate.Save(result.CandidateScreenshotPath, System.Drawing.Imaging.ImageFormat.Png);
                    result.ForegroundAfterCandidateCapture = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                    result.CandidateWindowDetected = result.CandidateChangedPixels >= candidateThreshold;
                    result.TerminalCompositionText = probe.ReadText();
                    result.TerminalDefaultKeepObserved =
                        result.ForegroundBeforeCandidateCapture &&
                        result.ForegroundAfterCandidateCapture &&
                        result.CandidateWindowDetected &&
                        !probe.TextMatches(result.TerminalCompositionText, options.InputText);
                }
            }

            if (!result.TerminalDefaultKeepObserved)
            {
                result.Errors.Add("Terminal context did not keep the explicitly selected Chinese mode.");
                return;
            }

            result.ForegroundBeforeCommit = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            if (!result.ForegroundBeforeCommit)
            {
                result.Errors.Add("Terminal lost foreground focus before the candidate commit.");
                return;
            }
            NativeMethods.PressKey(NativeMethods.VirtualKeySpace);
            Thread.Sleep(800);
            result.CommittedText = probe.ReadText();
            result.CommitMatched = probe.TextMatches(result.CommittedText, options.ExpectedText);
            if (!result.CommitMatched)
            {
                result.Errors.Add("The Terminal candidate did not commit the expected Chinese text.");
                return;
            }

            result.ForegroundBeforeEnglishSwitch = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            if (!result.ForegroundBeforeEnglishSwitch)
            {
                result.Errors.Add("Terminal lost foreground focus before the manual English switch.");
                return;
            }
            NativeMethods.PressKey(NativeMethods.VirtualKeyShift);
            Thread.Sleep(450);
            result.ForegroundBeforeEnglishInput = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            if (!result.ForegroundBeforeEnglishInput)
            {
                result.Errors.Add("Terminal lost foreground focus before the manual English input.");
                return;
            }
            NativeMethods.TypeAscii(options.ExpectedEnglish);
            Thread.Sleep(700);
            result.FinalText = probe.ReadText();
            result.EnglishModeMatched = probe.TextMatches(result.FinalText, options.ExpectedText + options.ExpectedEnglish);
            result.ForegroundAfterInput = NativeMethods.GetForegroundWindow() == probe.WindowHandle;

            if (result.EnglishModeMatched && result.ForegroundAfterInput)
            {
                NativeMethods.PressKey(NativeMethods.VirtualKeyShift);
                Thread.Sleep(450);
                result.ChineseModeRestored = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            }

            result.Passed = result.ProfileActivated && result.ForegroundAcquired && result.InitialCompositionDismissed &&
                result.FixturePrepared && result.ForegroundBeforePinyinInput && result.TerminalDefaultKeepObserved &&
                result.ForegroundBeforeCommit && result.CommitMatched && result.ForegroundBeforeEnglishSwitch &&
                result.ForegroundBeforeEnglishInput && result.EnglishModeMatched && result.ForegroundAfterInput &&
                result.ChineseModeRestored;
            if (!result.EnglishModeMatched) result.Errors.Add("Terminal manual English-mode text did not match the expected fixture.");
            if (result.EnglishModeMatched && !result.ChineseModeRestored) result.Errors.Add("Chinese mode was not restored after the Terminal English-mode check.");
        }

        private static void RunVsCodeContextScenario(AppSmokeOptions options, IAppProbe probe, AppSmokeResult result)
        {
            const string pinyin = "nihao";
            const string expectedCodePrefix = "nihaoconst existing = 1;";
            const string expectedCommentSuffix = "// 你好";
            const int candidateThreshold = 1500;
            const int automaticSettleMilliseconds = 750;

            int codeReportCount = CountAdapterContextReports(result, "surface=editor syntax=code");
            NativeMethods.PressChord(NativeMethods.VirtualKeyControl, NativeMethods.VirtualKeyEnd);
            NativeMethods.PressChord(NativeMethods.VirtualKeyControl, NativeMethods.VirtualKeyHome);
            result.VSCodeCodeContextReady = WaitForFreshAdapterContext(
                result, "surface=editor syntax=code", codeReportCount,
                options.TimeoutMilliseconds, out result.VSCodeCodeContextReadyElapsedMilliseconds);
            if (!result.VSCodeCodeContextReady)
            {
                result.Errors.Add("VS Code Adapter did not publish a fresh code context before input.");
                return;
            }
            Thread.Sleep(automaticSettleMilliseconds);
            result.ForegroundBeforeEnglishInput = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            if (!result.ForegroundBeforeEnglishInput)
            {
                result.Errors.Add("VS Code lost foreground focus before the code-context probe.");
                return;
            }

            NativeMethods.TypeAscii(pinyin);
            Thread.Sleep(650);
            result.CodeContextText = probe.ReadText();
            result.CodeContextEnglish = result.CodeContextText != null &&
                result.CodeContextText.StartsWith(expectedCodePrefix, StringComparison.Ordinal);
            result.AutomaticEnglishText = result.CodeContextText;
            result.AutomaticEnglishObserved = result.CodeContextEnglish;
            if (!result.CodeContextEnglish)
            {
                result.Errors.Add("VS Code code context did not automatically apply English mode.");
                return;
            }

            int commentReportCount = CountAdapterContextReports(result, "surface=editor syntax=comment");
            NativeMethods.PressChord(NativeMethods.VirtualKeyControl, NativeMethods.VirtualKeyEnd);
            result.VSCodeCommentContextReady = WaitForFreshAdapterContext(
                result, "surface=editor syntax=comment", commentReportCount,
                options.TimeoutMilliseconds, out result.VSCodeCommentContextReadyElapsedMilliseconds);
            if (!result.VSCodeCommentContextReady)
            {
                result.Errors.Add("VS Code Adapter did not publish a fresh comment context before input.");
                return;
            }
            Thread.Sleep(automaticSettleMilliseconds);
            result.ForegroundBeforePinyinInput = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            if (!result.ForegroundBeforePinyinInput)
            {
                result.Errors.Add("VS Code lost foreground focus before the comment-context probe.");
                return;
            }

            using (Bitmap clean = Capture(probe.CaptureRegion))
            {
                NativeMethods.TypeAscii(pinyin);
                Thread.Sleep(1000);
                result.ForegroundBeforeCandidateCapture = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                using (Bitmap candidate = Capture(probe.CaptureRegion))
                {
                    result.CommentCandidateChangedPixels = CountChangedPixels(clean, candidate);
                    result.CommentCandidateScreenshotPath = options.EvidencePath + ".comment-candidate.png";
                    candidate.Save(result.CommentCandidateScreenshotPath, System.Drawing.Imaging.ImageFormat.Png);
                }
            }
            result.CommentCandidateDetected = result.CommentCandidateChangedPixels >= candidateThreshold;
            result.CandidateChangedPixels = result.CommentCandidateChangedPixels;
            result.CandidateScreenshotPath = result.CommentCandidateScreenshotPath;
            result.CandidateWindowDetected = result.CommentCandidateDetected;
            if (!result.CommentCandidateDetected)
            {
                result.Errors.Add("VS Code comment context did not show a Chinese candidate/composition window.");
                return;
            }

            result.ForegroundBeforeCommit = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            if (!result.ForegroundBeforeCommit)
            {
                result.Errors.Add("VS Code lost foreground focus before the comment candidate commit.");
                return;
            }
            NativeMethods.PressKey(NativeMethods.VirtualKeySpace);
            Thread.Sleep(800);
            result.CommentContextText = probe.ReadText();
            result.CommentContextChinese = result.CommentContextText != null &&
                result.CommentContextText.StartsWith(expectedCodePrefix, StringComparison.Ordinal) &&
                result.CommentContextText.EndsWith(expectedCommentSuffix, StringComparison.Ordinal);
            result.CommittedText = result.CommentContextText;
            result.FinalText = result.CommentContextText;
            result.CommitMatched = result.CommentContextChinese;
            result.ForegroundAfterInput = NativeMethods.GetForegroundWindow() == probe.WindowHandle;

            result.Passed = result.VSCodeExtensionInstalled && result.VSCodeAdapterReady && result.ProfileActivated &&
                result.ForegroundAcquired && result.InitialCompositionDismissed && result.FixturePrepared &&
                result.VSCodeCodeContextReady && result.VSCodeCommentContextReady &&
                result.CodeContextEnglish && result.CommentCandidateDetected && result.CommentContextChinese &&
                result.ForegroundAfterInput;
            if (!result.CommentContextChinese)
            {
                result.Errors.Add("VS Code comment context did not commit simplified Chinese 你好.");
            }
        }

        private static void RunVsCodeProjectCandidateScenario(
            AppSmokeOptions options, IAppProbe probe, AppSmokeResult result)
        {
            const int candidateThreshold = 1500;
            Stopwatch clock = Stopwatch.StartNew();
            // Focus and move inside the editor before waiting for the Language
            // Server. A newly opened isolated VS Code window can activate the
            // Adapter before its first TextEditor interaction.
            NativeMethods.PressChord(NativeMethods.VirtualKeyControl, NativeMethods.VirtualKeyEnd);
            Thread.Sleep(250);
            string projectLine;
            result.VSCodeProjectSymbolsReady = WaitForAdapterLogLine(
                result, "[project:", "transport=ok", options.TimeoutMilliseconds,
                out projectLine, out result.VSCodeProjectSymbolsReadyElapsedMilliseconds);
            result.VSCodeProjectSymbolsLine = projectLine;
            if (!result.VSCodeProjectSymbolsReady ||
                projectLine.IndexOf("symbols=", StringComparison.Ordinal) < 0)
            {
                result.Errors.Add("VS Code Language Server symbols were not published to the Project Indexer.");
                return;
            }

            string workspacePath = Path.GetDirectoryName(result.ProbePath);
            result.ProjectId = CreateOpaqueProjectId(workspacePath);
            result.ProjectDictionaryPath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "ContextIME", "project-dictionaries", result.ProjectId + ".dict");
            string expectedSymbolHex = HexEncode(Encoding.UTF8.GetBytes(options.ExpectedText));
            result.ProjectDictionaryContainsSymbol = WaitForDictionarySymbol(
                result.ProjectDictionaryPath, expectedSymbolHex,
                options.TimeoutMilliseconds, out result.ProjectDictionaryReadyElapsedMilliseconds);
            if (!result.ProjectDictionaryContainsSymbol)
            {
                result.Errors.Add("The persisted active-project dictionary does not contain the expected Language Server symbol.");
                return;
            }
            result.ProjectDictionaryEvidencePath = options.EvidencePath + ".project-dictionary.dict";
            File.Copy(result.ProjectDictionaryPath, result.ProjectDictionaryEvidencePath, true);

            string commentLine;
            result.VSCodeCommentContextReady = WaitForAdapterLogLine(
                result, "surface=editor syntax=comment", "transport=ok response=ok",
                options.TimeoutMilliseconds, out commentLine,
                out result.VSCodeCommentContextReadyElapsedMilliseconds);
            if (!result.VSCodeCommentContextReady)
            {
                result.Errors.Add("VS Code Adapter did not publish comment context before project candidate input.");
                return;
            }

            Thread.Sleep(1000);
            result.ForegroundBeforePinyinInput = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            if (!result.ForegroundBeforePinyinInput)
            {
                result.Errors.Add("VS Code lost foreground focus before project candidate input.");
                return;
            }

            using (Bitmap clean = Capture(probe.CaptureRegion))
            {
                NativeMethods.TypeAscii(options.InputText);
                Thread.Sleep(1000);
                result.ForegroundBeforeCandidateCapture = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
                using (Bitmap candidate = Capture(probe.CaptureRegion))
                {
                    result.CandidateChangedPixels = CountChangedPixels(clean, candidate);
                    result.CandidateScreenshotPath = options.EvidencePath + ".project-candidate.png";
                    candidate.Save(result.CandidateScreenshotPath, System.Drawing.Imaging.ImageFormat.Png);
                }
            }
            result.ForegroundAfterCandidateCapture = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            result.CandidateWindowDetected = result.CandidateChangedPixels >= candidateThreshold;
            if (!result.CandidateWindowDetected)
            {
                result.Errors.Add("No significant project candidate/composition visual change was detected.");
                return;
            }

            result.ForegroundBeforeCommit = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            if (!result.ForegroundBeforeCommit)
            {
                result.Errors.Add("VS Code lost foreground focus before project candidate commit.");
                return;
            }
            NativeMethods.PressKey(NativeMethods.VirtualKeySpace);
            Thread.Sleep(800);
            result.CommittedText = probe.ReadText();
            result.FinalText = result.CommittedText;
            result.ProjectCandidateCommitMatched = result.CommittedText != null &&
                result.CommittedText.EndsWith("// " + options.ExpectedText, StringComparison.Ordinal);
            result.CommitMatched = result.ProjectCandidateCommitMatched;
            result.ForegroundAfterInput = NativeMethods.GetForegroundWindow() == probe.WindowHandle;
            result.ProjectCandidateElapsedMilliseconds = clock.ElapsedMilliseconds;
            result.Passed = result.VSCodeExtensionInstalled && result.VSCodeAdapterReady &&
                result.ProfileActivated && result.ForegroundAcquired && result.InitialCompositionDismissed &&
                result.FixturePrepared && result.VSCodeProjectSymbolsReady &&
                result.ProjectDictionaryContainsSymbol && result.VSCodeCommentContextReady &&
                result.CandidateWindowDetected && result.ProjectCandidateCommitMatched &&
                result.ForegroundAfterInput;
            if (!result.ProjectCandidateCommitMatched)
            {
                result.Errors.Add("The first project candidate did not commit the expected Language Server symbol.");
            }
        }

        private static bool WaitForAdapterLogLine(
            AppSmokeResult result, string firstMarker, string secondMarker,
            int timeoutMilliseconds, out string matchedLine,
            out long elapsedMilliseconds)
        {
            Stopwatch clock = Stopwatch.StartNew();
            matchedLine = String.Empty;
            while (clock.ElapsedMilliseconds < timeoutMilliseconds)
            {
                if (!String.IsNullOrEmpty(result.VSCodeAdapterLogPath) &&
                    File.Exists(result.VSCodeAdapterLogPath))
                {
                    try
                    {
                        using (FileStream stream = new FileStream(
                            result.VSCodeAdapterLogPath, FileMode.Open, FileAccess.Read,
                            FileShare.ReadWrite | FileShare.Delete))
                        using (StreamReader reader = new StreamReader(stream, Encoding.UTF8, true))
                        {
                            string line;
                            while ((line = reader.ReadLine()) != null)
                            {
                                if (line.Contains(firstMarker) && line.Contains(secondMarker))
                                {
                                    matchedLine = line;
                                    elapsedMilliseconds = clock.ElapsedMilliseconds;
                                    return true;
                                }
                            }
                        }
                    }
                    catch (IOException) { }
                    catch (UnauthorizedAccessException) { }
                }
                Thread.Sleep(50);
            }
            elapsedMilliseconds = clock.ElapsedMilliseconds;
            return false;
        }

        private static bool WaitForDictionarySymbol(
            string path, string symbolHex, int timeoutMilliseconds,
            out long elapsedMilliseconds)
        {
            Stopwatch clock = Stopwatch.StartNew();
            while (clock.ElapsedMilliseconds < timeoutMilliseconds)
            {
                try
                {
                    if (File.Exists(path))
                    {
                        string content = File.ReadAllText(path, Encoding.UTF8);
                        if (content.IndexOf("\tlanguage_server\t" + symbolHex, StringComparison.Ordinal) >= 0)
                        {
                            elapsedMilliseconds = clock.ElapsedMilliseconds;
                            return true;
                        }
                    }
                }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
                Thread.Sleep(50);
            }
            elapsedMilliseconds = clock.ElapsedMilliseconds;
            return false;
        }

        private static string CreateOpaqueProjectId(string workspacePath)
        {
            string normalized = Path.GetFullPath(workspacePath)
                .Replace('/', '\\').ToLowerInvariant();
            string pathRoot = Path.GetPathRoot(normalized);
            while (normalized.Length > pathRoot.Length && normalized.EndsWith("\\", StringComparison.Ordinal))
            {
                normalized = normalized.Substring(0, normalized.Length - 1);
            }
            byte[] input = Encoding.UTF8.GetBytes("ContextIME.Project.v1\0win32\0" + normalized);
            using (SHA256 sha256 = SHA256.Create())
            {
                byte[] digest = sha256.ComputeHash(input);
                byte[] projectId = new byte[16];
                Buffer.BlockCopy(digest, 0, projectId, 0, projectId.Length);
                return HexEncode(projectId);
            }
        }

        private static string HexEncode(byte[] bytes)
        {
            StringBuilder encoded = new StringBuilder(bytes.Length * 2);
            foreach (byte value in bytes) encoded.Append(value.ToString("x2"));
            return encoded.ToString();
        }

        private static bool WaitForFreshAdapterContext(
            AppSmokeResult result, string marker, int previousCount,
            int timeoutMilliseconds, out long elapsedMilliseconds)
        {
            Stopwatch clock = Stopwatch.StartNew();
            int contextTimeoutMilliseconds = Math.Min(timeoutMilliseconds, 5000);
            while (clock.ElapsedMilliseconds < contextTimeoutMilliseconds)
            {
                if (CountAdapterContextReports(result, marker) > previousCount)
                {
                    elapsedMilliseconds = clock.ElapsedMilliseconds;
                    return true;
                }
                Thread.Sleep(25);
            }
            elapsedMilliseconds = clock.ElapsedMilliseconds;
            return false;
        }

        private static int CountAdapterContextReports(AppSmokeResult result, string marker)
        {
            if (String.IsNullOrEmpty(result.VSCodeAdapterLogPath) ||
                !File.Exists(result.VSCodeAdapterLogPath)) return 0;
            try
            {
                int count = 0;
                using (FileStream stream = new FileStream(
                    result.VSCodeAdapterLogPath, FileMode.Open, FileAccess.Read,
                    FileShare.ReadWrite | FileShare.Delete))
                using (StreamReader reader = new StreamReader(stream, Encoding.UTF8, true))
                {
                    string line;
                    while ((line = reader.ReadLine()) != null)
                    {
                        if (line.Contains(marker) &&
                            line.Contains("transport=ok response=ok")) count += 1;
                    }
                }
                return count;
            }
            catch (IOException) { return 0; }
            catch (UnauthorizedAccessException) { return 0; }
        }

        private static bool AcquireForegroundForCleanup(IAppProbe probe, AppSmokeResult result)
        {
            for (int attempt = 1; attempt <= 3; attempt++)
            {
                result.CleanupForegroundAcquireAttempts = attempt;
                if (probe.AcquireForeground()) return true;
                Thread.Sleep(250);
            }
            return false;
        }

        private static bool DismissInitialComposition(IAppProbe probe, AppSmokeResult result)
        {
            for (int attempt = 1; attempt <= 3; attempt++)
            {
                result.InitialCompositionDismissAttempts = attempt;
                if (NativeMethods.GetForegroundWindow() != probe.WindowHandle && !probe.AcquireForeground())
                {
                    Thread.Sleep(250);
                    continue;
                }

                NativeMethods.PressKey(NativeMethods.VirtualKeyEscape);
                Thread.Sleep(250);
                if (NativeMethods.GetForegroundWindow() == probe.WindowHandle) return true;
                Thread.Sleep(250);
            }
            return false;
        }

        private static Bitmap Capture(Rectangle rectangle)
        {
            Bitmap bitmap = new Bitmap(rectangle.Width, rectangle.Height, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (Graphics graphics = Graphics.FromImage(bitmap))
            {
                graphics.CopyFromScreen(rectangle.Location, Point.Empty, rectangle.Size, CopyPixelOperation.SourceCopy);
            }
            return bitmap;
        }

        private static int CountChangedPixels(Bitmap baseline, Bitmap current)
        {
            if (baseline.Size != current.Size) return 0;
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

        private static string HResult(int value)
        {
            return "0x" + unchecked((uint)value).ToString("x8");
        }

        private static string Handle(IntPtr value)
        {
            return "0x" + unchecked((ulong)value.ToInt64()).ToString("x");
        }
    }

    internal interface IAppProbe
    {
        IntPtr WindowHandle { get; }
        Rectangle CaptureRegion { get; }
        bool AcquireForeground();
        void PrepareFixture();
        string ReadText();
        bool TextMatches(string actual, string expected);
        void Close();
    }

    internal sealed class NotepadProbe : IAppProbe
    {
        private readonly AppSmokeOptions options;
        private readonly IntPtr windowHandle;
        private readonly AutomationElement root;
        private readonly AutomationElement document;
        private readonly string probeFileName;

        public IntPtr WindowHandle { get { return windowHandle; } }
        public Rectangle CaptureRegion { get; private set; }

        private NotepadProbe(AppSmokeOptions options, IntPtr windowHandle, AutomationElement root, AutomationElement document, string probeFileName)
        {
            this.options = options;
            this.windowHandle = windowHandle;
            this.root = root;
            this.document = document;
            this.probeFileName = probeFileName;
            System.Windows.Rect bounds = document.Current.BoundingRectangle;
            Rectangle screen = ScreenBounds(bounds);
            CaptureRegion = new Rectangle(screen.Left, screen.Top, Math.Max(screen.Width, 1), Math.Max(screen.Height, 1));
        }

        public static NotepadProbe Open(AppSmokeOptions options, AppSmokeResult result)
        {
            string probeFileName = "contextime-notepad-smoke-" + Process.GetCurrentProcess().Id + ".txt";
            string probePath = Path.Combine(Path.GetTempPath(), probeFileName);
            File.WriteAllText(probePath, String.Empty, new UTF8Encoding(false));
            Process.Start(new ProcessStartInfo
            {
                FileName = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32", "notepad.exe"),
                Arguments = "\"" + probePath + "\"",
                UseShellExecute = true
            });

            Stopwatch clock = Stopwatch.StartNew();
            while (clock.ElapsedMilliseconds < options.TimeoutMilliseconds)
            {
                foreach (Process candidate in Process.GetProcessesByName("Notepad"))
                {
                    try
                    {
                        IntPtr candidateWindow = candidate.MainWindowHandle;
                        if (candidateWindow == IntPtr.Zero) continue;
                        AutomationElement root = AutomationElement.FromHandle(candidateWindow);
                        AutomationElement tab = FindSelectedTab(root, probeFileName);
                        if (tab == null) continue;
                        AutomationElement document = root.FindFirst(
                            TreeScope.Descendants,
                            new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Document));
                        if (document == null) continue;

                        Thread.Sleep(750);
                        candidate.Refresh();
                        uint stableWindowProcessId;
                        IntPtr stableWindow = candidate.MainWindowHandle;
                        if (candidate.HasExited || stableWindow == IntPtr.Zero || stableWindow != candidateWindow ||
                            !NativeMethods.IsWindow(stableWindow) ||
                            NativeMethods.GetWindowThreadProcessId(stableWindow, out stableWindowProcessId) == 0 ||
                            stableWindowProcessId != candidate.Id)
                        {
                            continue;
                        }
                        root = AutomationElement.FromHandle(stableWindow);
                        tab = FindSelectedTab(root, probeFileName);
                        if (tab == null) continue;
                        document = root.FindFirst(
                            TreeScope.Descendants,
                            new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Document));
                        if (document == null) continue;

                        result.ApplicationProcessId = candidate.Id;
                        result.ApplicationPath = SafeProcessPath(candidate);
                        result.ApplicationVersion = SafeProcessVersion(candidate);
                        result.WindowTitle = candidate.MainWindowTitle;
                        result.ProbePath = probePath;
                        return new NotepadProbe(options, candidateWindow, root, document, probeFileName);
                    }
                    catch
                    {
                    }
                    finally
                    {
                        if (candidate != null) candidate.Dispose();
                    }
                }
                Thread.Sleep(200);
            }
            throw new TimeoutException("Could not find the isolated Notepad probe tab.");
        }

        public bool AcquireForeground()
        {
            return ProbeUtilities.AcquireForeground(WindowHandle, document) && document.Current.HasKeyboardFocus;
        }

        public string ReadText()
        {
            object patternObject;
            if (!document.TryGetCurrentPattern(TextPattern.Pattern, out patternObject)) throw new InvalidOperationException("Notepad document has no TextPattern.");
            TextPattern pattern = (TextPattern)patternObject;
            return pattern.DocumentRange.GetText(-1).TrimEnd('\r', '\n');
        }

        public void PrepareFixture()
        {
        }

        public bool TextMatches(string actual, string expected)
        {
            return String.Equals(actual, expected, StringComparison.Ordinal);
        }

        public void Close()
        {
            IntPtr targetWindow = WindowHandle;
            if (!NativeMethods.IsWindow(targetWindow)) return;
            if (!AcquireForegroundForClose()) throw new InvalidOperationException("Could not focus Notepad for cleanup.");
            NativeMethods.PressKey(NativeMethods.VirtualKeyEscape);
            Thread.Sleep(250);
            if (!NativeMethods.IsWindow(targetWindow)) return;
            AutomationElement currentRoot = AutomationElement.FromHandle(targetWindow);
            AutomationElement tab = FindSelectedTab(currentRoot, probeFileName);
            if (tab == null) return;
            AutomationElement closeButton = tab.FindFirst(
                TreeScope.Descendants,
                new PropertyCondition(AutomationElement.AutomationIdProperty, "CloseButton"));
            object invokeObject;
            if (closeButton != null && closeButton.TryGetCurrentPattern(InvokePattern.Pattern, out invokeObject))
            {
                ((InvokePattern)invokeObject).Invoke();
            }
            else
            {
                if (!AcquireForegroundForClose()) throw new InvalidOperationException("Could not focus Notepad for the Ctrl+W cleanup fallback.");
                NativeMethods.PressChord(NativeMethods.VirtualKeyControl, (byte)'W');
            }

            bool probeTabClosed = false;
            Stopwatch clock = Stopwatch.StartNew();
            while (clock.ElapsedMilliseconds < 3000)
            {
                Thread.Sleep(150);
                if (!NativeMethods.IsWindow(targetWindow)) return;
                try
                {
                    currentRoot = AutomationElement.FromHandle(targetWindow);
                    AutomationElement dismiss = FindButton(currentRoot, "不保存", "Don't Save", "Don’t Save");
                    if (dismiss != null)
                    {
                        object dismissInvoke;
                        if (dismiss.TryGetCurrentPattern(InvokePattern.Pattern, out dismissInvoke)) ((InvokePattern)dismissInvoke).Invoke();
                    }
                    if (FindTab(currentRoot, probeFileName) == null)
                    {
                        probeTabClosed = true;
                        break;
                    }
                }
                catch (ElementNotAvailableException)
                {
                    if (!NativeMethods.IsWindow(targetWindow)) return;
                    throw;
                }
                catch (COMException)
                {
                    if (!NativeMethods.IsWindow(targetWindow)) return;
                    throw;
                }
            }
            if (!probeTabClosed) throw new TimeoutException("The Notepad probe tab did not close.");
            if (!NativeMethods.IsWindow(targetWindow)) return;
            currentRoot = AutomationElement.FromHandle(targetWindow);
            ProbeUtilities.CloseWindow(currentRoot, targetWindow);
        }

        private bool AcquireForegroundForClose()
        {
            for (int attempt = 0; attempt < 3; attempt++)
            {
                if (AcquireForeground()) return true;
                Thread.Sleep(250);
            }
            return false;
        }

        private static AutomationElement FindSelectedTab(AutomationElement root, string fileName)
        {
            AutomationElement tab = FindTab(root, fileName);
            if (tab == null) return null;
            object selectionObject;
            if (!tab.TryGetCurrentPattern(SelectionItemPattern.Pattern, out selectionObject)) return null;
            return ((SelectionItemPattern)selectionObject).Current.IsSelected ? tab : null;
        }

        private static AutomationElement FindTab(AutomationElement root, string fileName)
        {
            AutomationElementCollection tabs = root.FindAll(
                TreeScope.Descendants,
                new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.TabItem));
            for (int index = 0; index < tabs.Count; index++)
            {
                if (tabs[index].Current.Name.StartsWith(fileName + ".", StringComparison.OrdinalIgnoreCase)) return tabs[index];
            }
            return null;
        }

        private static AutomationElement FindButton(AutomationElement root, params string[] names)
        {
            AutomationElementCollection buttons = root.FindAll(
                TreeScope.Descendants,
                new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Button));
            for (int index = 0; index < buttons.Count; index++)
            {
                foreach (string name in names)
                {
                    if (String.Equals(buttons[index].Current.Name, name, StringComparison.OrdinalIgnoreCase)) return buttons[index];
                }
            }
            return null;
        }

        private static Rectangle ScreenBounds(System.Windows.Rect bounds)
        {
            Rectangle screen = System.Windows.Forms.Screen.FromPoint(new Point((int)bounds.Left, (int)bounds.Top)).Bounds;
            int left = Math.Max(screen.Left, (int)Math.Floor(bounds.Left));
            int top = Math.Max(screen.Top, (int)Math.Floor(bounds.Top));
            int right = Math.Min(screen.Right, (int)Math.Ceiling(bounds.Right));
            int bottom = Math.Min(screen.Bottom, (int)Math.Ceiling(bounds.Bottom));
            return Rectangle.FromLTRB(left, top, Math.Max(left + 1, right), Math.Max(top + 1, bottom));
        }

        private static string SafeProcessPath(Process process)
        {
            try { return process.MainModule.FileName; }
            catch { return String.Empty; }
        }

        private static string SafeProcessVersion(Process process)
        {
            try { return process.MainModule.FileVersionInfo.FileVersion; }
            catch { return String.Empty; }
        }
    }

    internal abstract class WindowProbeBase : IAppProbe
    {
        protected readonly Process ApplicationProcess;
        protected readonly AutomationElement Root;
        protected readonly AutomationElement FocusElement;

        public IntPtr WindowHandle { get; private set; }
        public Rectangle CaptureRegion { get; private set; }

        protected WindowProbeBase(Process process, AutomationElement root, AutomationElement focusElement, Rectangle captureRegion)
        {
            ApplicationProcess = process;
            Root = root;
            FocusElement = focusElement;
            WindowHandle = new IntPtr(root.Current.NativeWindowHandle);
            CaptureRegion = captureRegion;
        }

        public virtual bool AcquireForeground()
        {
            return ProbeUtilities.AcquireForeground(WindowHandle, FocusElement);
        }

        public virtual void PrepareFixture()
        {
        }

        public abstract string ReadText();

        public virtual bool TextMatches(string actual, string expected)
        {
            return String.Equals(actual, expected, StringComparison.Ordinal);
        }

        public virtual void Close()
        {
            ProbeUtilities.CloseWindow(Root, WindowHandle);
        }
    }

    internal sealed class BrowserProbe : WindowProbeBase
    {
        private readonly AutomationElement textField;
        private readonly string valueMarker;

        private BrowserProbe(Process process, AutomationElement root, AutomationElement textField, string valueMarker)
            : base(process, root, textField, ProbeUtilities.CaptureInside(root, textField, 900, 600))
        {
            this.textField = textField;
            this.valueMarker = valueMarker;
        }

        public static BrowserProbe Open(AppSmokeOptions options, AppSmokeResult result)
        {
            string edgePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Microsoft", "Edge", "Application", "msedge.exe");
            if (!File.Exists(edgePath)) throw new FileNotFoundException("Microsoft Edge is not installed.", edgePath);
            string title = "ContextIMEBrowserSmoke" + Process.GetCurrentProcess().Id;
            string valueMarker = title + "|";
            string probePath = Path.Combine(Path.GetTempPath(), title + ".html");
            string html = "<!doctype html><html><head><meta charset=\"utf-8\"><title>" + title + "</title></head>" +
                "<body><label for=\"smoke\">ContextIME Browser Smoke</label>" +
                "<textarea id=\"smoke\" autofocus style=\"display:block;width:640px;height:220px;font-size:32px\"></textarea>" +
                "<script>const box=document.getElementById('smoke');box.addEventListener('input',()=>document.title='" + valueMarker + "'+box.value);</script>" +
                "</body></html>";
            File.WriteAllText(probePath, html, new UTF8Encoding(false));
            string url = new Uri(probePath).AbsoluteUri;
            string userData = Path.Combine(Path.GetTempPath(), title + "-profile");
            Directory.CreateDirectory(userData);
            Process.Start(new ProcessStartInfo
            {
                FileName = edgePath,
                Arguments = "--user-data-dir=\"" + userData + "\" --inprivate --new-window --no-first-run --disable-extensions --force-renderer-accessibility=complete \"" + url + "\"",
                UseShellExecute = true
            });

            Process process;
            AutomationElement root = ProbeUtilities.WaitForProcessWindow("msedge", title, options.TimeoutMilliseconds, out process);
            IntPtr windowHandle = new IntPtr(root.Current.NativeWindowHandle);
            try
            {
                AutomationElement textField = null;
                List<string> editDiagnostics = new List<string>();
                Stopwatch fieldClock = Stopwatch.StartNew();
                while (fieldClock.ElapsedMilliseconds < 10000 && textField == null)
                {
                    AutomationElementCollection edits = root.FindAll(
                        TreeScope.Descendants,
                        new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Edit));
                    for (int index = 0; index < edits.Count; index++)
                    {
                        System.Windows.Rect bounds = edits[index].Current.BoundingRectangle;
                        string diagnostic = edits[index].Current.ClassName + "|" + edits[index].Current.AutomationId + "|" +
                            bounds.ToString() + "|offscreen=" + edits[index].Current.IsOffscreen;
                        if (!editDiagnostics.Contains(diagnostic)) editDiagnostics.Add(diagnostic);
                        if (String.Equals(edits[index].Current.ClassName, "Textfield", StringComparison.OrdinalIgnoreCase))
                        {
                            textField = edits[index];
                            break;
                        }
                    }
                    if (textField == null) Thread.Sleep(200);
                }
                if (textField == null) throw new InvalidOperationException("Could not find the Edge textarea. UIA edits: " + String.Join("; ", editDiagnostics.ToArray()));

                ProbeUtilities.RecordProcess(result, process, root, probePath);
                return new BrowserProbe(process, root, textField, valueMarker);
            }
            catch
            {
                try { ProbeUtilities.CloseWindow(root, windowHandle); }
                catch { }
                throw;
            }
        }

        public override string ReadText()
        {
            string title = Root.Current.Name;
            int markerIndex = title.IndexOf(valueMarker, StringComparison.Ordinal);
            if (markerIndex >= 0)
            {
                int valueStart = markerIndex + valueMarker.Length;
                int valueEnd = title.IndexOf(" - [InPrivate]", valueStart, StringComparison.Ordinal);
                if (valueEnd < 0) valueEnd = title.Length;
                return title.Substring(valueStart, valueEnd - valueStart);
            }
            object valueObject;
            if (!textField.TryGetCurrentPattern(ValuePattern.Pattern, out valueObject)) throw new InvalidOperationException("The Edge textarea has no ValuePattern.");
            return ((ValuePattern)valueObject).Current.Value;
        }

        public override bool AcquireForeground()
        {
            return ProbeUtilities.AcquireForeground(WindowHandle, textField);
        }
    }

    internal sealed class VsCodeProbe : WindowProbeBase
    {
        private readonly string probePath;

        private VsCodeProbe(Process process, AutomationElement root, string probePath)
            : base(process, root, null, ProbeUtilities.WindowBounds(root))
        {
            this.probePath = probePath;
        }

        public static VsCodeProbe Open(AppSmokeOptions options, AppSmokeResult result)
        {
            string codePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "Microsoft VS Code", "Code.exe");
            if (!File.Exists(codePath)) throw new FileNotFoundException("Visual Studio Code is not installed.", codePath);
            bool contextScenario = options.Scenario == "vscode-context";
            bool projectCandidateScenario = options.Scenario == "vscode-project-candidate";
            bool adapterScenario = contextScenario || projectCandidateScenario;
            string probeName = "contextime-vscode-smoke-" + Process.GetCurrentProcess().Id + (adapterScenario ? ".ts" : ".txt");
            string probeRoot = Path.Combine(Path.GetTempPath(), Path.GetFileNameWithoutExtension(probeName));
            string userData = Path.Combine(probeRoot, "user-data");
            string extensions = Path.Combine(probeRoot, "extensions");
            string workspaceRoot = projectCandidateScenario ? Path.Combine(probeRoot, "workspace") : probeRoot;
            string probePath = Path.Combine(workspaceRoot, probeName);
            Directory.CreateDirectory(userData);
            Directory.CreateDirectory(extensions);
            Directory.CreateDirectory(workspaceRoot);
            File.WriteAllText(
                probePath,
                contextScenario ? "const existing = 1;\r\n// " :
                    projectCandidateScenario ? "class PlayerController {\r\n  spawnPlayer() {}\r\n}\r\n\r\n// " : String.Empty,
                new UTF8Encoding(false));

            if (adapterScenario)
            {
                string codeCommand = Path.Combine(Path.GetDirectoryName(codePath), "bin", "code.cmd");
                if (!File.Exists(codeCommand)) throw new FileNotFoundException("Visual Studio Code CLI is not installed.", codeCommand);
                string commandInterpreter = Environment.GetEnvironmentVariable("ComSpec");
                if (String.IsNullOrEmpty(commandInterpreter)) commandInterpreter = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "cmd.exe");
                ProcessStartInfo installInfo = new ProcessStartInfo
                {
                    FileName = commandInterpreter,
                    Arguments = "/d /s /c \"\"" + codeCommand + "\" --install-extension \"" + options.VSCodeExtensionPath +
                        "\" --force --user-data-dir \"" + userData + "\" --extensions-dir \"" + extensions + "\"\"",
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true
                };
                using (Process installProcess = Process.Start(installInfo))
                {
                    if (!installProcess.WaitForExit(90000))
                    {
                        try { installProcess.Kill(); } catch { }
                        throw new TimeoutException("VS Code extension installation exceeded 90 seconds.");
                    }
                    result.VSCodeExtensionInstallExitCode = installProcess.ExitCode;
                    result.VSCodeExtensionInstallOutput = installProcess.StandardOutput.ReadToEnd() + installProcess.StandardError.ReadToEnd();
                }
                string[] installedDirectories = Directory.GetDirectories(extensions, "kun002.context-ime-*");
                result.VSCodeExtensionInstalled = result.VSCodeExtensionInstallExitCode == 0 && installedDirectories.Length == 1;
                result.VSCodeExtensionDirectory = installedDirectories.Length == 1 ? installedDirectories[0] : String.Empty;
                if (!result.VSCodeExtensionInstalled)
                {
                    throw new InvalidOperationException("The isolated ContextIME VS Code Adapter installation failed: " + result.VSCodeExtensionInstallOutput);
                }
            }

            Process.Start(new ProcessStartInfo
            {
                FileName = codePath,
                Arguments = "--new-window " + (adapterScenario ? String.Empty : "--disable-extensions ") +
                    (projectCandidateScenario ? "--disable-workspace-trust " : String.Empty) +
                    "--skip-welcome --user-data-dir \"" + userData + "\" --extensions-dir \"" + extensions + "\" " +
                    (projectCandidateScenario ? "\"" + workspaceRoot + "\" " : String.Empty) + "\"" + probePath + "\"",
                UseShellExecute = true
            });

            Process process;
            AutomationElement root = ProbeUtilities.WaitForProcessWindow("Code", probeName, options.TimeoutMilliseconds, out process);
            if (adapterScenario)
            {
                WaitForContextAdapter(userData, options.TimeoutMilliseconds, result);
            }
            ProbeUtilities.RecordProcess(result, process, root, probePath);
            return new VsCodeProbe(process, root, probePath);
        }

        private static void WaitForContextAdapter(string userData, int timeoutMilliseconds, AppSmokeResult result)
        {
            string logRoot = Path.Combine(userData, "logs");
            Stopwatch clock = Stopwatch.StartNew();
            while (clock.ElapsedMilliseconds < timeoutMilliseconds)
            {
                if (Directory.Exists(logRoot))
                {
                    string[] logs;
                    try
                    {
                        logs = Directory.GetFiles(logRoot, "*ContextIME Adapter.log", SearchOption.AllDirectories);
                    }
                    catch (IOException)
                    {
                        logs = new string[0];
                    }
                    catch (UnauthorizedAccessException)
                    {
                        logs = new string[0];
                    }

                    foreach (string logPath in logs)
                    {
                        try
                        {
                            string content;
                            using (FileStream stream = new FileStream(logPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                            using (StreamReader reader = new StreamReader(stream, Encoding.UTF8, true))
                            {
                                content = reader.ReadToEnd();
                            }
                            foreach (string line in content.Split(new[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries))
                            {
                                if (line.Contains("[report:activate]") &&
                                    line.Contains("transport=ok response=ok"))
                                {
                                    result.VSCodeAdapterReady = true;
                                    result.VSCodeAdapterReadyElapsedMilliseconds = clock.ElapsedMilliseconds;
                                    result.VSCodeAdapterLogPath = logPath;
                                    result.VSCodeAdapterReadyLine = line;
                                    return;
                                }
                            }
                        }
                        catch (IOException)
                        {
                        }
                        catch (UnauthorizedAccessException)
                        {
                        }
                    }
                }
                Thread.Sleep(100);
            }
            throw new TimeoutException("The isolated VS Code Adapter did not activate and reach Context Service successfully.");
        }

        public override bool AcquireForeground()
        {
            if (!ProbeUtilities.AcquireForeground(WindowHandle, null)) return false;
            NativeMethods.PressChord(NativeMethods.VirtualKeyControl, (byte)'1');
            Thread.Sleep(300);
            return NativeMethods.GetForegroundWindow() == WindowHandle;
        }

        public override string ReadText()
        {
            NativeMethods.PressChord(NativeMethods.VirtualKeyControl, (byte)'S');
            Thread.Sleep(500);
            return File.ReadAllText(probePath, Encoding.UTF8).TrimEnd('\r', '\n');
        }

        public override void Close()
        {
            NativeMethods.PressChord(NativeMethods.VirtualKeyControl, (byte)'S');
            Thread.Sleep(250);
            base.Close();
        }
    }

    internal sealed class VisualStudioProbe : WindowProbeBase
    {
        private readonly string probePath;

        private VisualStudioProbe(Process process, AutomationElement root, AutomationElement editor, string probePath)
            : base(process, root, editor, ProbeUtilities.CaptureInside(root, editor, 900, 600, 64, 240))
        {
            this.probePath = probePath;
        }

        public static VisualStudioProbe Open(AppSmokeOptions options, AppSmokeResult result)
        {
            string visualStudioPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Microsoft Visual Studio", "2022", "Community", "Common7", "IDE", "devenv.exe");
            if (!File.Exists(visualStudioPath)) throw new FileNotFoundException("Visual Studio 2022 Community is not installed.", visualStudioPath);
            string probeName = "contextime-visualstudio-smoke-" + Process.GetCurrentProcess().Id + ".txt";
            string probeRoot = Path.Combine(Path.GetTempPath(), Path.GetFileNameWithoutExtension(probeName));
            string probePath = Path.Combine(probeRoot, probeName);
            Directory.CreateDirectory(probeRoot);
            File.WriteAllText(probePath, String.Empty, new UTF8Encoding(true));

            Process.Start(new ProcessStartInfo
            {
                FileName = visualStudioPath,
                Arguments = "/nosplash \"" + probePath + "\"",
                UseShellExecute = true
            });

            Process process;
            AutomationElement root = ProbeUtilities.WaitForProcessWindow("devenv", probeName, options.TimeoutMilliseconds, out process);
            ProbeUtilities.DismissButton(root, "ButtonClose");
            AutomationElement editor = ProbeUtilities.WaitForElement(
                root,
                new PropertyCondition(AutomationElement.AutomationIdProperty, "WpfTextView"),
                options.TimeoutMilliseconds);
            ProbeUtilities.RecordProcess(result, process, root, probePath);
            return new VisualStudioProbe(process, root, editor, probePath);
        }

        public override string ReadText()
        {
            NativeMethods.PressChord(NativeMethods.VirtualKeyControl, (byte)'S');
            Thread.Sleep(600);
            return File.ReadAllText(probePath, Encoding.UTF8).TrimEnd('\r', '\n');
        }

        public override void PrepareFixture()
        {
            for (int index = 0; index < 8; index++) NativeMethods.PressKey(NativeMethods.VirtualKeyReturn);
            Thread.Sleep(250);
        }

        public override bool TextMatches(string actual, string expected)
        {
            return String.Equals(actual.Trim('\r', '\n'), expected, StringComparison.Ordinal);
        }

        public override void Close()
        {
            NativeMethods.PressChord(NativeMethods.VirtualKeyControl, (byte)'S');
            Thread.Sleep(250);
            base.Close();
        }
    }

    internal sealed class TerminalProbe : WindowProbeBase
    {
        private readonly AutomationElement terminalControl;

        private TerminalProbe(Process process, AutomationElement root, AutomationElement terminalControl)
            : base(process, root, terminalControl, ProbeUtilities.CaptureInside(root, terminalControl, 900, 600))
        {
            this.terminalControl = terminalControl;
        }

        public static TerminalProbe Open(AppSmokeOptions options, AppSmokeResult result)
        {
            string terminalPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Microsoft", "WindowsApps", "wt.exe");
            if (!File.Exists(terminalPath)) throw new FileNotFoundException("Windows Terminal is not installed.", terminalPath);
            string title = "ContextIMETerminalSmoke" + Process.GetCurrentProcess().Id;
            Process.Start(new ProcessStartInfo
            {
                FileName = terminalPath,
                Arguments = "-w new nt --title " + title + " powershell.exe -NoLogo -NoProfile",
                UseShellExecute = true
            });

            Process process;
            AutomationElement root = ProbeUtilities.WaitForProcessWindow("WindowsTerminal", title, options.TimeoutMilliseconds, out process);
            AutomationElement terminalControl = root.FindFirst(
                TreeScope.Descendants,
                new PropertyCondition(AutomationElement.ClassNameProperty, "TermControl"));
            if (terminalControl == null) throw new InvalidOperationException("Could not find the Windows Terminal control.");
            ProbeUtilities.RecordProcess(result, process, root, String.Empty);
            return new TerminalProbe(process, root, terminalControl);
        }

        public override string ReadText()
        {
            object textObject;
            if (!terminalControl.TryGetCurrentPattern(TextPattern.Pattern, out textObject)) throw new InvalidOperationException("The Windows Terminal control has no TextPattern.");
            return ((TextPattern)textObject).DocumentRange.GetText(-1).TrimEnd('\r', '\n', ' ');
        }

        public override bool TextMatches(string actual, string expected)
        {
            return actual.EndsWith(expected, StringComparison.Ordinal);
        }
    }

    internal static class ProbeUtilities
    {
        public static bool AcquireForeground(IntPtr windowHandle, AutomationElement focusElement)
        {
            NativeMethods.ShowWindow(windowHandle, NativeMethods.ShowWindowRestore);
            NativeMethods.SwitchToThisWindow(windowHandle, true);
            Thread.Sleep(150);
            IntPtr foreground = NativeMethods.GetForegroundWindow();
            uint foregroundProcess;
            uint foregroundThread = foreground == IntPtr.Zero ? 0 : NativeMethods.GetWindowThreadProcessId(foreground, out foregroundProcess);
            uint targetProcess;
            uint targetThread = NativeMethods.GetWindowThreadProcessId(windowHandle, out targetProcess);
            uint currentThread = NativeMethods.GetCurrentThreadId();
            bool attachedForeground = false;
            bool attachedTarget = false;
            try
            {
                if (foregroundThread != 0 && foregroundThread != currentThread) attachedForeground = NativeMethods.AttachThreadInput(currentThread, foregroundThread, true);
                if (targetThread != 0 && targetThread != currentThread && targetThread != foregroundThread) attachedTarget = NativeMethods.AttachThreadInput(currentThread, targetThread, true);
                NativeMethods.BringWindowToTop(windowHandle);
                NativeMethods.SetForegroundWindow(windowHandle);
                if (focusElement != null)
                {
                    try { focusElement.SetFocus(); }
                    catch
                    {
                        try
                        {
                            System.Windows.Rect bounds = focusElement.Current.BoundingRectangle;
                            if (!bounds.IsEmpty && bounds.Width > 0 && bounds.Height > 0) NativeMethods.ClickCenter(bounds);
                        }
                        catch
                        {
                        }
                    }
                }
            }
            finally
            {
                if (attachedTarget) NativeMethods.AttachThreadInput(currentThread, targetThread, false);
                if (attachedForeground) NativeMethods.AttachThreadInput(currentThread, foregroundThread, false);
            }
            Thread.Sleep(300);
            if (NativeMethods.GetForegroundWindow() == windowHandle) return true;
            if (focusElement != null)
            {
                try
                {
                    System.Windows.Rect bounds = focusElement.Current.BoundingRectangle;
                    if (!bounds.IsEmpty && bounds.Width > 0 && bounds.Height > 0)
                    {
                        NativeMethods.ClickCenter(bounds);
                        Thread.Sleep(300);
                    }
                }
                catch
                {
                }
            }
            return NativeMethods.GetForegroundWindow() == windowHandle;
        }

        public static AutomationElement WaitForProcessWindow(string processName, string titleFragment, int timeoutMilliseconds, out Process process)
        {
            Stopwatch clock = Stopwatch.StartNew();
            while (clock.ElapsedMilliseconds < timeoutMilliseconds)
            {
                foreach (Process candidate in Process.GetProcessesByName(processName))
                {
                    if (candidate.MainWindowHandle != IntPtr.Zero && candidate.MainWindowTitle.IndexOf(titleFragment, StringComparison.OrdinalIgnoreCase) >= 0)
                    {
                        process = candidate;
                        return AutomationElement.FromHandle(candidate.MainWindowHandle);
                    }
                    candidate.Dispose();
                }
                Thread.Sleep(250);
            }
            process = null;
            throw new TimeoutException("Could not find the " + processName + " probe window containing '" + titleFragment + "'.");
        }

        public static AutomationElement WaitForDesktopWindow(string titleFragment, int timeoutMilliseconds)
        {
            Stopwatch clock = Stopwatch.StartNew();
            while (clock.ElapsedMilliseconds < timeoutMilliseconds)
            {
                AutomationElementCollection windows = AutomationElement.RootElement.FindAll(
                    TreeScope.Children,
                    new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Window));
                for (int index = 0; index < windows.Count; index++)
                {
                    if (windows[index].Current.Name.IndexOf(titleFragment, StringComparison.OrdinalIgnoreCase) >= 0) return windows[index];
                }
                Thread.Sleep(250);
            }
            throw new TimeoutException("Could not find the desktop probe window containing '" + titleFragment + "'.");
        }

        public static AutomationElement WaitForElement(AutomationElement root, Condition condition, int timeoutMilliseconds)
        {
            Stopwatch clock = Stopwatch.StartNew();
            while (clock.ElapsedMilliseconds < timeoutMilliseconds)
            {
                AutomationElement element = root.FindFirst(TreeScope.Descendants, condition);
                if (element != null) return element;
                Thread.Sleep(250);
            }
            throw new TimeoutException("Could not find the requested application element.");
        }

        public static void DismissButton(AutomationElement root, string automationId)
        {
            AutomationElement button = root.FindFirst(
                TreeScope.Descendants,
                new PropertyCondition(AutomationElement.AutomationIdProperty, automationId));
            if (button == null) return;
            object invokeObject;
            if (button.TryGetCurrentPattern(InvokePattern.Pattern, out invokeObject))
            {
                ((InvokePattern)invokeObject).Invoke();
                Thread.Sleep(600);
            }
        }

        public static Rectangle CaptureAround(AutomationElement element, int requestedWidth, int requestedHeight)
        {
            System.Windows.Rect bounds = element.Current.BoundingRectangle;
            Point origin = new Point((int)Math.Floor(bounds.Left), (int)Math.Floor(bounds.Top));
            Rectangle screen = System.Windows.Forms.Screen.FromPoint(origin).Bounds;
            int left = Math.Max(screen.Left, Math.Min(origin.X, screen.Right - 1));
            int top = Math.Max(screen.Top, Math.Min(origin.Y, screen.Bottom - 1));
            int width = Math.Max(1, Math.Min(requestedWidth, screen.Right - left));
            int height = Math.Max(1, Math.Min(requestedHeight, screen.Bottom - top));
            return new Rectangle(left, top, width, height);
        }

        public static Rectangle CaptureInside(AutomationElement root, AutomationElement focusElement, int requestedWidth, int requestedHeight)
        {
            return CaptureInside(root, focusElement, requestedWidth, requestedHeight, 0, 0);
        }

        public static Rectangle CaptureInside(
            AutomationElement root,
            AutomationElement focusElement,
            int requestedWidth,
            int requestedHeight,
            int leftMargin,
            int topMargin)
        {
            System.Windows.Rect rootBounds = root.Current.BoundingRectangle;
            System.Windows.Rect focusBounds = focusElement.Current.BoundingRectangle;
            Point rootOrigin = new Point((int)Math.Floor(rootBounds.Left), (int)Math.Floor(rootBounds.Top));
            Rectangle screen = System.Windows.Forms.Screen.FromPoint(rootOrigin).Bounds;
            const int inset = 16;
            int minimumLeft = Math.Max(screen.Left, (int)Math.Ceiling(rootBounds.Left) + inset);
            int minimumTop = Math.Max(screen.Top, (int)Math.Ceiling(rootBounds.Top) + inset);
            int maximumRight = Math.Min(screen.Right, (int)Math.Floor(rootBounds.Right) - inset);
            int maximumBottom = Math.Min(screen.Bottom, (int)Math.Floor(rootBounds.Bottom) - inset);
            if (maximumRight <= minimumLeft || maximumBottom <= minimumTop) throw new InvalidOperationException("The application window has no safe interior capture region.");

            int preferredLeft = (int)Math.Floor(focusBounds.Left) - Math.Max(leftMargin, 0);
            int preferredTop = (int)Math.Floor(focusBounds.Top) - Math.Max(topMargin, 0);
            int left = Math.Max(minimumLeft, Math.Min(preferredLeft, maximumRight - 1));
            int top = Math.Max(minimumTop, Math.Min(preferredTop, maximumBottom - 1));
            int width = Math.Max(1, Math.Min(requestedWidth, maximumRight - left));
            int height = Math.Max(1, Math.Min(requestedHeight, maximumBottom - top));
            return new Rectangle(left, top, width, height);
        }

        public static Rectangle WindowBounds(AutomationElement root)
        {
            System.Windows.Rect bounds = root.Current.BoundingRectangle;
            Point origin = new Point((int)Math.Floor(bounds.Left), (int)Math.Floor(bounds.Top));
            Rectangle screen = System.Windows.Forms.Screen.FromPoint(origin).Bounds;
            int left = Math.Max(screen.Left, origin.X);
            int top = Math.Max(screen.Top, origin.Y);
            int right = Math.Min(screen.Right, (int)Math.Ceiling(bounds.Right));
            int bottom = Math.Min(screen.Bottom, (int)Math.Ceiling(bounds.Bottom));
            return Rectangle.FromLTRB(left, top, Math.Max(left + 1, right), Math.Max(top + 1, bottom));
        }

        public static void RecordProcess(AppSmokeResult result, Process process, AutomationElement root, string probePath)
        {
            result.ApplicationProcessId = process.Id;
            result.ApplicationPath = SafeProcessPath(process);
            result.ApplicationVersion = SafeProcessVersion(process);
            result.WindowTitle = root.Current.Name;
            result.ProbePath = probePath;
        }

        public static void CloseWindow(AutomationElement root, IntPtr windowHandle)
        {
            object windowObject;
            if (root.TryGetCurrentPattern(WindowPattern.Pattern, out windowObject))
            {
                ((WindowPattern)windowObject).Close();
            }
            else
            {
                NativeMethods.PostMessage(windowHandle, NativeMethods.WindowMessageClose, IntPtr.Zero, IntPtr.Zero);
            }

            Stopwatch clock = Stopwatch.StartNew();
            while (clock.ElapsedMilliseconds < 5000)
            {
                if (!NativeMethods.IsWindow(windowHandle)) return;
                Thread.Sleep(150);
            }
            throw new TimeoutException("The isolated application window did not close.");
        }

        private static string SafeProcessPath(Process process)
        {
            try { return process.MainModule.FileName; }
            catch { return String.Empty; }
        }

        private static string SafeProcessVersion(Process process)
        {
            try { return process.MainModule.FileVersionInfo.FileVersion; }
            catch { return String.Empty; }
        }
    }

    internal sealed class AppSmokeResult
    {
        public string SchemaVersion;
        public string StartedUtc;
        public string CompletedUtc;
        public string Application;
        public string Scenario;
        public int ApplicationProcessId;
        public string ApplicationPath;
        public string ApplicationVersion;
        public string WindowTitle;
        public string ProbePath;
        public string InputText;
        public string ExpectedText;
        public string ExpectedEnglish;
        public string VSCodeExtensionDirectory;
        public string VSCodeExtensionInstallOutput;
        public int VSCodeExtensionInstallExitCode;
        public bool VSCodeExtensionInstalled;
        public bool VSCodeAdapterReady;
        public long VSCodeAdapterReadyElapsedMilliseconds;
        public string VSCodeAdapterLogPath;
        public string VSCodeAdapterReadyLine;
        public string CodeContextText;
        public bool CodeContextEnglish;
        public bool VSCodeCodeContextReady;
        public long VSCodeCodeContextReadyElapsedMilliseconds;
        public bool VSCodeCommentContextReady;
        public long VSCodeCommentContextReadyElapsedMilliseconds;
        public bool VSCodeProjectSymbolsReady;
        public long VSCodeProjectSymbolsReadyElapsedMilliseconds;
        public string VSCodeProjectSymbolsLine;
        public string ProjectId;
        public string ProjectDictionaryPath;
        public string ProjectDictionaryEvidencePath;
        public bool ProjectDictionaryContainsSymbol;
        public long ProjectDictionaryReadyElapsedMilliseconds;
        public bool ProjectCandidateCommitMatched;
        public long ProjectCandidateElapsedMilliseconds;
        public int CommentCandidateChangedPixels;
        public string CommentCandidateScreenshotPath;
        public bool CommentCandidateDetected;
        public string CommentContextText;
        public bool CommentContextChinese;
        public string CommittedText;
        public string FinalText;
        public string OriginalProfileQueryHResult;
        public ProfileEvidence OriginalProfile;
        public string ProfileActivationHResult;
        public string ActiveProfileQueryHResult;
        public ProfileEvidence ActiveProfile;
        public string ProfileRestoreHResult;
        public bool ProfileActivated;
        public bool ProfileRestored;
        public bool ForegroundAcquired;
        public bool InitialCompositionDismissed;
        public int InitialCompositionDismissAttempts;
        public bool FixturePrepared;
        public bool ForegroundBeforePinyinInput;
        public bool ForegroundBeforeCandidateCapture;
        public bool ForegroundAfterCandidateCapture;
        public bool ForegroundBeforeCommit;
        public bool ForegroundBeforeEnglishSwitch;
        public bool ForegroundBeforeEnglishInput;
        public bool ForegroundAfterInput;
        public string RequestedWindowHandle;
        public string ForegroundWindowHandleAfterAcquire;
        public string FocusedElementNameAfterAcquire;
        public int FocusedElementProcessIdAfterAcquire;
        public bool CandidateWindowDetected;
        public int CandidateChangedPixels;
        public string CandidateScreenshotPath;
        public string TerminalCompositionText;
        public bool TerminalDefaultKeepObserved;
        public bool CommitMatched;
        public bool EnglishModeMatched;
        public bool ChineseModeRestored;
        public string AutomaticEnglishText;
        public bool AutomaticEnglishObserved;
        public bool CleanupForegroundAcquired;
        public int CleanupForegroundAcquireAttempts;
        public bool CompositionDismissedBeforeClose;
        public bool ProbeClosed;
        public bool Passed;
        public List<string> Errors;

        public static AppSmokeResult Create(AppSmokeOptions options)
        {
            return new AppSmokeResult
            {
                SchemaVersion = options.Scenario == "baseline" ? "contextime.app-smoke.v1" :
                    options.Scenario == "vscode-project-candidate" ? "contextime.app-smoke.v3" : "contextime.app-smoke.v2",
                StartedUtc = DateTime.UtcNow.ToString("o"),
                Application = options.Application,
                Scenario = options.Scenario,
                InputText = options.InputText,
                ExpectedText = options.ExpectedText,
                ExpectedEnglish = options.ExpectedEnglish,
                Errors = new List<string>()
            };
        }
    }

    internal sealed class ProfileEvidence
    {
        public uint ProfileType;
        public string Language;
        public string ClassId;
        public string Profile;
        public string KeyboardLayout;
        public uint Capabilities;
        public uint Flags;

        public static ProfileEvidence FromProfile(TfInputProcessorProfile profile)
        {
            return new ProfileEvidence
            {
                ProfileType = profile.ProfileType,
                Language = "0x" + profile.Language.ToString("x4"),
                ClassId = profile.ClassId.ToString("D").ToUpperInvariant(),
                Profile = profile.Profile.ToString("D").ToUpperInvariant(),
                KeyboardLayout = "0x" + unchecked((ulong)profile.KeyboardLayout.ToInt64()).ToString("x"),
                Capabilities = profile.Capabilities,
                Flags = profile.Flags
            };
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TfInputProcessorProfile
    {
        public uint ProfileType;
        public ushort Language;
        public Guid ClassId;
        public Guid Profile;
        public Guid Category;
        public IntPtr SubstituteKeyboardLayout;
        public uint Capabilities;
        public IntPtr KeyboardLayout;
        public uint Flags;
    }

    [ComImport]
    [Guid("71C6E74C-0F28-11D8-A82A-00065B84435C")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface ITfInputProcessorProfileMgr
    {
        [PreserveSig] int ActivateProfile(uint profileType, ushort language, ref Guid classId, ref Guid profile, IntPtr keyboardLayout, uint flags);
        [PreserveSig] int DeactivateProfile(uint profileType, ushort language, ref Guid classId, ref Guid profile, IntPtr keyboardLayout, uint flags);
        [PreserveSig] int GetProfile(uint profileType, ushort language, ref Guid classId, ref Guid profile, IntPtr keyboardLayout, out TfInputProcessorProfile result);
        [PreserveSig] int EnumProfiles(ushort language, out IntPtr profiles);
        [PreserveSig] int ReleaseInputProcessor(ref Guid classId, uint flags);
        [PreserveSig] int RegisterProfile(ref Guid classId, ushort language, ref Guid profile, IntPtr description, uint descriptionLength, IntPtr iconFile, uint iconFileLength, uint iconIndex, IntPtr substituteKeyboardLayout, uint preferredLayout, [MarshalAs(UnmanagedType.Bool)] bool enabledByDefault, uint flags);
        [PreserveSig] int UnregisterProfile(ref Guid classId, ushort language, ref Guid profile, uint flags);
        [PreserveSig] int GetActiveProfile(ref Guid category, out TfInputProcessorProfile profile);
    }

    internal static class NativeMethods
    {
        internal const byte VirtualKeyShift = 0x10;
        internal const byte VirtualKeyControl = 0x11;
        internal const byte VirtualKeyReturn = 0x0D;
        internal const byte VirtualKeyEscape = 0x1B;
        internal const byte VirtualKeySpace = 0x20;
        internal const byte VirtualKeyEnd = 0x23;
        internal const byte VirtualKeyHome = 0x24;
        internal const uint WindowMessageClose = 0x0010;
        internal const int ShowWindowRestore = 9;
        private const uint KeyEventKeyUp = 0x0002;
        private const uint MouseEventLeftDown = 0x0002;
        private const uint MouseEventLeftUp = 0x0004;

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool SetForegroundWindow(IntPtr window);

        [DllImport("user32.dll")]
        internal static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool BringWindowToTop(IntPtr window);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool ShowWindow(IntPtr window, int command);

        [DllImport("user32.dll")]
        internal static extern void SwitchToThisWindow(IntPtr window, [MarshalAs(UnmanagedType.Bool)] bool altTab);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool IsWindow(IntPtr window);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool AttachThreadInput(uint attachThread, uint attachToThread, [MarshalAs(UnmanagedType.Bool)] bool attach);

        [DllImport("kernel32.dll")]
        internal static extern uint GetCurrentThreadId();

        [DllImport("user32.dll")]
        internal static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

        [DllImport("user32.dll")]
        internal static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

        [DllImport("user32.dll")]
        internal static extern uint MapVirtualKey(uint code, uint mapType);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        internal static extern short VkKeyScan(char character);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool SetCursorPos(int x, int y);

        [DllImport("user32.dll")]
        internal static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extraInfo);

        internal static void PressKey(byte virtualKey)
        {
            byte scanCode = (byte)MapVirtualKey(virtualKey, 0);
            keybd_event(virtualKey, scanCode, 0, UIntPtr.Zero);
            Thread.Sleep(35);
            keybd_event(virtualKey, scanCode, KeyEventKeyUp, UIntPtr.Zero);
            Thread.Sleep(50);
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

        internal static void ClickCenter(System.Windows.Rect bounds)
        {
            int x = (int)Math.Round(bounds.Left + bounds.Width / 2.0);
            int y = (int)Math.Round(bounds.Top + bounds.Height / 2.0);
            SetCursorPos(x, y);
            Thread.Sleep(50);
            mouse_event(MouseEventLeftDown, 0, 0, 0, UIntPtr.Zero);
            Thread.Sleep(35);
            mouse_event(MouseEventLeftUp, 0, 0, 0, UIntPtr.Zero);
            Thread.Sleep(100);
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
